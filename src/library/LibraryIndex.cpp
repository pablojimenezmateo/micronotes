#include "CoreAliases.h"
#include "library/LibraryIndex.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/AppIdentity.h"
#include "core/persistence/SqliteDb.h"
#include "core/perf/PerformanceCounters.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace micronotes::library {

using persistence::SqliteDb;
using persistence::Statement;

namespace {

static void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

static std::string columnText(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

static std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

static void fillSnippet(SearchResult& result, std::string_view body, std::string_view query) {
  if(query.empty()) return;
  const auto lowerQuery = lowerCopy(std::string(query));

  std::vector<std::string> lines;
  std::istringstream in {std::string(body)};
  std::string line;
  while(std::getline(in, line)) lines.push_back(line);
  for(std::size_t i = 0; i < lines.size(); ++i) {
    if(lowerCopy(lines[i]).find(lowerQuery) != std::string::npos) {
      SearchResult::Snippet snippet;
      if(i > 0) snippet.beforeLine = lines[i - 1];
      snippet.matchLine = lines[i];
      if(i + 1 < lines.size()) snippet.afterLine = lines[i + 1];
      result.snippets.push_back(snippet);
    }
  }
  if(!result.snippets.empty()) {
    result.beforeLine = result.snippets.front().beforeLine;
    result.matchLine = result.snippets.front().matchLine;
    result.afterLine = result.snippets.front().afterLine;
  }
}

static void collectRows(sqlite3_stmt* stmt, std::vector<SearchResult>& out, std::string_view query) {
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back({
      reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
      {},
      reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
    });
    out.back().path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const unsigned char* body = sqlite3_column_text(stmt, 3);
    if(body) fillSnippet(out.back(), reinterpret_cast<const char*>(body), query);
  }
}

}

bool LibraryIndex::open(const std::filesystem::path& libraryRoot) {
  root_ = libraryRoot;
  std::filesystem::create_directories(root_ / microcore::kAppDotDir);
  dbPath_ = root_ / microcore::kAppDotDir / "index.sqlite";
  // The one connection every later call reuses. SqliteDb::open applies the
  // per-connection pragmas, which is the only place they can take effect.
  if(!db_.open(dbPath_)) return false;
  return migrate();
}

bool LibraryIndex::migrate() {
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  // No PRAGMA statements here: journal_mode, synchronous and foreign_keys are
  // applied by SqliteDb::open, because the latter two are per-connection and
  // setting them during migration configured only migration's own connection.
  const bool ok =
    db.exec("CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, path TEXT NOT NULL, title TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, body TEXT NOT NULL);") &&
    db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(id UNINDEXED, title, body, path);");
  return ok;
}

bool LibraryIndex::rebuild() {
  perf::ScopeTimer timer("library_index.rebuild");
  perf::addCounter(perf::CounterId::LibraryIndexRebuilds);
  rows_.clear();
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  if(!db.exec("BEGIN IMMEDIATE; DELETE FROM notes; DELETE FROM notes_fts;")) {
    return false;
  }

  Statement noteStmt = db.prepare("INSERT INTO notes(id,path,title,mtime,size,body) VALUES(?,?,?,?,?,?);");
  Statement ftsStmt = db.prepare("INSERT INTO notes_fts(id,title,body,path) VALUES(?,?,?,?);");

  Library library(root_);
  for(const auto& path : library.noteFiles()) {
    perf::addCounter(perf::CounterId::LibraryIndexFilesScanned);
    const auto note = library.loadNote(path);
    const auto relative = path.lexically_relative(root_).generic_string();
    // Files written by other tools have no front matter; index them anyway.
    const auto noteId = note.metadata.id.empty() ? fallbackNoteId(relative) : note.metadata.id;
    const auto stat = std::filesystem::status(path);
    const auto mtime = std::filesystem::last_write_time(path).time_since_epoch().count();
    const auto size = static_cast<long long>(std::filesystem::file_size(path));
    const auto title = note.metadata.title.empty() ? path.stem().string() : note.metadata.title;

    sqlite3_reset(noteStmt);
    bindText(noteStmt, 1, noteId);
    bindText(noteStmt, 2, relative);
    bindText(noteStmt, 3, title);
    sqlite3_bind_int64(noteStmt, 4, static_cast<sqlite3_int64>(mtime));
    sqlite3_bind_int64(noteStmt, 5, static_cast<sqlite3_int64>(size));
    bindText(noteStmt, 6, note.body);
    sqlite3_step(noteStmt);

    sqlite3_reset(ftsStmt);
    bindText(ftsStmt, 1, noteId);
    bindText(ftsStmt, 2, title);
    bindText(ftsStmt, 3, note.body);
    bindText(ftsStmt, 4, relative);
    sqlite3_step(ftsStmt);

    rows_.push_back({noteId, path, title});
    (void)stat;
  }

  const bool ok = db.exec("COMMIT;");
  return ok;
}

bool LibraryIndex::refreshChangedFiles() {
  perf::ScopeTimer timer("library_index.refresh_changed_files");
  perf::addCounter(perf::CounterId::LibraryIndexRefreshCalls);
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;

  struct ExistingRow {
    std::string id;
    long long mtime = 0;
    long long size = 0;
  };
  std::unordered_map<std::string, ExistingRow> existing;
  {
  perf::ScopeTimer loadTimer("library_index.refresh.load_existing");
  if(Statement selectStmt = db.prepare("SELECT path,id,mtime,size FROM notes;"); selectStmt) {
    while(sqlite3_step(selectStmt) == SQLITE_ROW) {
      existing.emplace(columnText(selectStmt, 0), ExistingRow {
        columnText(selectStmt, 1),
        sqlite3_column_int64(selectStmt, 2),
        sqlite3_column_int64(selectStmt, 3),
      });
    }
  }
  }

  // Pass one: work out what changed, touching nothing. This used to run inside
  // BEGIN IMMEDIATE, so every refresh took the write lock and forced a WAL
  // commit -- even the overwhelmingly common case where the answer is "nothing
  // changed". Refresh runs on every window focus, so that was a write per
  // focus, for nothing.
  struct ChangedFile {
    std::filesystem::path path;
    std::string relative;
    long long mtime = 0;
    long long size = 0;
  };
  std::vector<ChangedFile> changed;
  std::unordered_set<std::string> seenPaths;
  std::unordered_set<std::string> seenIds;
  Library library(root_);

  {
  perf::ScopeTimer scanTimer("library_index.refresh.scan_tree");
  for(const auto& entry : library.noteFileEntries()) {
    const auto& path = entry.path();
    auto relative = path.lexically_relative(root_).generic_string();
    // Both of these read the directory_entry's cached stat, so this is one
    // syscall per file rather than the two the free functions used to make.
    std::error_code error;
    const auto mtime = entry.last_write_time(error).time_since_epoch().count();
    const auto size = static_cast<long long>(entry.file_size(error));
    if(error) continue;
    perf::addCounter(perf::CounterId::LibraryIndexFilesScanned);

    const auto found = existing.find(relative);
    if(found != existing.end() && found->second.mtime == mtime && found->second.size == size) {
      seenIds.insert(found->second.id);
      seenPaths.insert(std::move(relative));
      continue;
    }
    seenPaths.insert(relative);
    changed.push_back(ChangedFile {path, std::move(relative), mtime, size});
  }
  }

  std::vector<std::pair<std::string, std::string>> removed;  // relative path, id
  for(const auto& [path, row] : existing) {
    if(seenPaths.contains(path)) continue;
    removed.emplace_back(path, row.id);
  }

  bool ok = true;
  const bool hasWork = !changed.empty() || !removed.empty();

  // Pass two: apply, in one transaction, only if there is anything to apply.
  if(hasWork) {
    if(!db.exec("BEGIN IMMEDIATE;")) return false;

    Statement upsertStmt = db.prepare("INSERT INTO notes(id,path,title,mtime,size,body) VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,title=excluded.title,mtime=excluded.mtime,size=excluded.size,body=excluded.body;");
    Statement deleteFtsStmt = db.prepare("DELETE FROM notes_fts WHERE id=?;");
    Statement insertFtsStmt = db.prepare("INSERT INTO notes_fts(id,title,body,path) VALUES(?,?,?,?);");
    Statement deleteNoteStmt = db.prepare("DELETE FROM notes WHERE path=?;");
    ok = upsertStmt && deleteFtsStmt && insertFtsStmt && deleteNoteStmt;

    if(ok) {
      for(const auto& file : changed) {
        perf::addCounter(perf::CounterId::LibraryIndexFilesReread);
        const auto note = library.loadNote(file.path);
        // A note without a front-matter id still belongs in the index; it gets
        // an id derived from its path rather than being skipped.
        const auto noteId = note.metadata.id.empty() ? fallbackNoteId(file.relative) : note.metadata.id;
        seenIds.insert(noteId);
        const auto title = note.metadata.title.empty() ? file.path.stem().string() : note.metadata.title;

        sqlite3_reset(upsertStmt);
        bindText(upsertStmt, 1, noteId);
        bindText(upsertStmt, 2, file.relative);
        bindText(upsertStmt, 3, title);
        sqlite3_bind_int64(upsertStmt, 4, static_cast<sqlite3_int64>(file.mtime));
        sqlite3_bind_int64(upsertStmt, 5, static_cast<sqlite3_int64>(file.size));
        bindText(upsertStmt, 6, note.body);
        ok = sqlite3_step(upsertStmt) == SQLITE_DONE;
        if(!ok) break;

        sqlite3_reset(deleteFtsStmt);
        bindText(deleteFtsStmt, 1, noteId);
        ok = sqlite3_step(deleteFtsStmt) == SQLITE_DONE;
        if(!ok) break;

        sqlite3_reset(insertFtsStmt);
        bindText(insertFtsStmt, 1, noteId);
        bindText(insertFtsStmt, 2, title);
        bindText(insertFtsStmt, 3, note.body);
        bindText(insertFtsStmt, 4, file.relative);
        ok = sqlite3_step(insertFtsStmt) == SQLITE_DONE;
        if(!ok) break;
      }
    }

    if(ok) {
      for(const auto& [path, id] : removed) {
        // A note that moved keeps its id under a new path; the upsert above
        // already rewrote the row, so deleting by the old path would drop it.
        if(seenIds.contains(id)) continue;
        perf::addCounter(perf::CounterId::LibraryIndexRowsDeleted);
        sqlite3_reset(deleteNoteStmt);
        bindText(deleteNoteStmt, 1, path);
        ok = sqlite3_step(deleteNoteStmt) == SQLITE_DONE;
        if(!ok) break;
        sqlite3_reset(deleteFtsStmt);
        bindText(deleteFtsStmt, 1, id);
        ok = sqlite3_step(deleteFtsStmt) == SQLITE_DONE;
        if(!ok) break;
      }
    }

    ok = ok && db.exec("COMMIT;");
    if(!ok) db.exec("ROLLBACK;");
  }

  // rows_ is a projection of the table. Reloading it means 1000 rows and 1000
  // heap allocations, so only do it when the table actually moved -- or when we
  // have never loaded it.
  if(ok && (hasWork || rows_.empty())) {
    rows_.clear();
    if(Statement selectStmt = db.prepare("SELECT id,path,title FROM notes ORDER BY title;"); selectStmt) {
      while(sqlite3_step(selectStmt) == SQLITE_ROW) {
        rows_.push_back({columnText(selectStmt, 0), root_ / columnText(selectStmt, 1), columnText(selectStmt, 2)});
      }
    }
  }
  return ok;
}

std::vector<SearchResult> LibraryIndex::search(std::string_view query, SearchScope scope) const {
  perf::ScopeTimer timer("library_index.search");
  perf::addCounter(perf::CounterId::LibrarySearchCalls);
  std::vector<SearchResult> out;
  if(query.empty()) return out;
  if(db_.isOpen()) {
    SqliteDb& db = db_;
    {
      const char* ftsSql = scope == SearchScope::Title
        ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts.title MATCH ? ORDER BY rank LIMIT 200;"
        : scope == SearchScope::Content
          ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts.body MATCH ? ORDER BY rank LIMIT 200;"
          : "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts MATCH ? ORDER BY rank LIMIT 200;";
      if(Statement stmt = db.prepare(ftsSql); stmt) {
        const std::string q(query);
        bindText(stmt, 1, q);
        collectRows(stmt, out, query);
      }
      if(out.empty()) {
        const char* likeSql = scope == SearchScope::Title
          ? "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ? ORDER BY title LIMIT 200;"
          : scope == SearchScope::Content
            ? "SELECT id,path,title,body FROM notes WHERE lower(body) LIKE ? ORDER BY title LIMIT 200;"
            : "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ? OR lower(body) LIKE ? OR lower(path) LIKE ? ORDER BY title LIMIT 200;";
        if(Statement stmt = db.prepare(likeSql); stmt) {
          std::string q = lowerCopy(std::string(query));
          q = "%" + q + "%";
          bindText(stmt, 1, q);
          if(scope == SearchScope::All) {
            bindText(stmt, 2, q);
            bindText(stmt, 3, q);
          }
          collectRows(stmt, out, query);
        }
      }
      for(auto& result : out) result.path = root_ / result.path;
      return out;
    }
  }
  for(const auto& row : rows_) {
    if((scope != SearchScope::Content && row.title.find(query) != std::string::npos) ||
       (scope == SearchScope::All && row.path.string().find(query) != std::string::npos)) {
      out.push_back(row);
    }
  }
  return out;
}

std::size_t LibraryIndex::size() const {
  return rows_.size();
}

}
