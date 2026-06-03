#include "library/LibraryIndex.h"

#include "library/Library.h"
#include "perf/Perf.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace micronotes::library {

namespace {

static bool exec(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  sqlite3_free(error);
  return rc == SQLITE_OK;
}

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
  std::filesystem::create_directories(root_ / ".micronotes");
  dbPath_ = root_ / ".micronotes" / "index.sqlite";
  return migrate();
}

bool LibraryIndex::migrate() {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  const bool ok =
    exec(db, "PRAGMA journal_mode=WAL;") &&
    exec(db, "PRAGMA synchronous=NORMAL;") &&
    exec(db, "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, path TEXT NOT NULL, title TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, body TEXT NOT NULL);") &&
    exec(db, "CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(id UNINDEXED, title, body, path);");
  sqlite3_close(db);
  return ok;
}

bool LibraryIndex::rebuild() {
  perf::ScopeTimer timer("library_index.rebuild");
  rows_.clear();
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  if(!exec(db, "BEGIN IMMEDIATE; DELETE FROM notes; DELETE FROM notes_fts;")) {
    sqlite3_close(db);
    return false;
  }

  sqlite3_stmt* noteStmt = nullptr;
  sqlite3_stmt* ftsStmt = nullptr;
  sqlite3_prepare_v2(db, "INSERT INTO notes(id,path,title,mtime,size,body) VALUES(?,?,?,?,?,?);", -1, &noteStmt, nullptr);
  sqlite3_prepare_v2(db, "INSERT INTO notes_fts(id,title,body,path) VALUES(?,?,?,?);", -1, &ftsStmt, nullptr);

  Library library(root_);
  for(const auto& path : library.noteFiles()) {
    const auto note = library.loadNote(path);
    if(note.metadata.id.empty()) continue;
    const auto relative = path.lexically_relative(root_).generic_string();
    const auto stat = std::filesystem::status(path);
    const auto mtime = std::filesystem::last_write_time(path).time_since_epoch().count();
    const auto size = static_cast<long long>(std::filesystem::file_size(path));
    const auto title = note.metadata.title.empty() ? path.stem().string() : note.metadata.title;

    sqlite3_reset(noteStmt);
    bindText(noteStmt, 1, note.metadata.id);
    bindText(noteStmt, 2, relative);
    bindText(noteStmt, 3, title);
    sqlite3_bind_int64(noteStmt, 4, static_cast<sqlite3_int64>(mtime));
    sqlite3_bind_int64(noteStmt, 5, static_cast<sqlite3_int64>(size));
    bindText(noteStmt, 6, note.body);
    sqlite3_step(noteStmt);

    sqlite3_reset(ftsStmt);
    bindText(ftsStmt, 1, note.metadata.id);
    bindText(ftsStmt, 2, title);
    bindText(ftsStmt, 3, note.body);
    bindText(ftsStmt, 4, relative);
    sqlite3_step(ftsStmt);

    rows_.push_back({note.metadata.id, path, title});
    (void)stat;
  }

  sqlite3_finalize(noteStmt);
  sqlite3_finalize(ftsStmt);
  const bool ok = exec(db, "COMMIT;");
  sqlite3_close(db);
  return ok;
}

bool LibraryIndex::refreshChangedFiles() {
  perf::ScopeTimer timer("library_index.refresh_changed_files");
  rows_.clear();
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;

  struct ExistingRow {
    std::string id;
    long long mtime = 0;
    long long size = 0;
  };
  std::unordered_map<std::string, ExistingRow> existing;
  sqlite3_stmt* selectStmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT path,id,mtime,size FROM notes;", -1, &selectStmt, nullptr) == SQLITE_OK) {
    while(sqlite3_step(selectStmt) == SQLITE_ROW) {
      existing.emplace(columnText(selectStmt, 0), ExistingRow {
        columnText(selectStmt, 1),
        sqlite3_column_int64(selectStmt, 2),
        sqlite3_column_int64(selectStmt, 3),
      });
    }
  }
  if(selectStmt) sqlite3_finalize(selectStmt);

  if(!exec(db, "BEGIN IMMEDIATE;")) {
    sqlite3_close(db);
    return false;
  }

  sqlite3_stmt* upsertStmt = nullptr;
  sqlite3_stmt* deleteFtsStmt = nullptr;
  sqlite3_stmt* insertFtsStmt = nullptr;
  sqlite3_stmt* deleteNoteStmt = nullptr;
  sqlite3_stmt* deleteRemovedFtsStmt = nullptr;
  sqlite3_prepare_v2(db, "INSERT INTO notes(id,path,title,mtime,size,body) VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,title=excluded.title,mtime=excluded.mtime,size=excluded.size,body=excluded.body;", -1, &upsertStmt, nullptr);
  sqlite3_prepare_v2(db, "DELETE FROM notes_fts WHERE id=?;", -1, &deleteFtsStmt, nullptr);
  sqlite3_prepare_v2(db, "INSERT INTO notes_fts(id,title,body,path) VALUES(?,?,?,?);", -1, &insertFtsStmt, nullptr);
  sqlite3_prepare_v2(db, "DELETE FROM notes WHERE path=?;", -1, &deleteNoteStmt, nullptr);
  sqlite3_prepare_v2(db, "DELETE FROM notes_fts WHERE id=?;", -1, &deleteRemovedFtsStmt, nullptr);

  bool ok = upsertStmt && deleteFtsStmt && insertFtsStmt && deleteNoteStmt && deleteRemovedFtsStmt;
  std::unordered_set<std::string> seenPaths;
  std::unordered_set<std::string> seenIds;
  Library library(root_);
  if(ok) {
    for(const auto& path : library.noteFiles()) {
      const auto relative = path.lexically_relative(root_).generic_string();
      seenPaths.insert(relative);
      const auto mtime = std::filesystem::last_write_time(path).time_since_epoch().count();
      const auto size = static_cast<long long>(std::filesystem::file_size(path));
      const auto found = existing.find(relative);
      if(found != existing.end() && found->second.mtime == mtime && found->second.size == size) {
        seenIds.insert(found->second.id);
        continue;
      }

      const auto note = library.loadNote(path);
      if(note.metadata.id.empty()) continue;
      seenIds.insert(note.metadata.id);
      const auto title = note.metadata.title.empty() ? path.stem().string() : note.metadata.title;

      sqlite3_reset(upsertStmt);
      bindText(upsertStmt, 1, note.metadata.id);
      bindText(upsertStmt, 2, relative);
      bindText(upsertStmt, 3, title);
      sqlite3_bind_int64(upsertStmt, 4, static_cast<sqlite3_int64>(mtime));
      sqlite3_bind_int64(upsertStmt, 5, static_cast<sqlite3_int64>(size));
      bindText(upsertStmt, 6, note.body);
      ok = sqlite3_step(upsertStmt) == SQLITE_DONE;
      if(!ok) break;

      sqlite3_reset(deleteFtsStmt);
      bindText(deleteFtsStmt, 1, note.metadata.id);
      ok = sqlite3_step(deleteFtsStmt) == SQLITE_DONE;
      if(!ok) break;

      sqlite3_reset(insertFtsStmt);
      bindText(insertFtsStmt, 1, note.metadata.id);
      bindText(insertFtsStmt, 2, title);
      bindText(insertFtsStmt, 3, note.body);
      bindText(insertFtsStmt, 4, relative);
      ok = sqlite3_step(insertFtsStmt) == SQLITE_DONE;
      if(!ok) break;
    }
  }

  if(ok) {
    for(const auto& [path, row] : existing) {
      if(seenPaths.contains(path)) continue;
      if(seenIds.contains(row.id)) continue;
      sqlite3_reset(deleteNoteStmt);
      bindText(deleteNoteStmt, 1, path);
      ok = sqlite3_step(deleteNoteStmt) == SQLITE_DONE;
      if(!ok) break;
      sqlite3_reset(deleteRemovedFtsStmt);
      bindText(deleteRemovedFtsStmt, 1, row.id);
      ok = sqlite3_step(deleteRemovedFtsStmt) == SQLITE_DONE;
      if(!ok) break;
    }
  }

  sqlite3_finalize(upsertStmt);
  sqlite3_finalize(deleteFtsStmt);
  sqlite3_finalize(insertFtsStmt);
  sqlite3_finalize(deleteNoteStmt);
  sqlite3_finalize(deleteRemovedFtsStmt);
  ok = ok && exec(db, "COMMIT;");
  if(!ok) exec(db, "ROLLBACK;");

  if(ok && sqlite3_prepare_v2(db, "SELECT id,path,title FROM notes ORDER BY title;", -1, &selectStmt, nullptr) == SQLITE_OK) {
    while(sqlite3_step(selectStmt) == SQLITE_ROW) {
      rows_.push_back({columnText(selectStmt, 0), root_ / columnText(selectStmt, 1), columnText(selectStmt, 2)});
    }
  }
  if(selectStmt) sqlite3_finalize(selectStmt);
  sqlite3_close(db);
  return ok;
}

void LibraryIndex::add(SearchResult result) {
  rows_.push_back(std::move(result));
}

std::vector<SearchResult> LibraryIndex::search(std::string_view query, SearchScope scope) const {
  perf::ScopeTimer timer("library_index.search");
  std::vector<SearchResult> out;
  if(query.empty()) return out;
  if(!dbPath_.empty() && std::filesystem::exists(dbPath_)) {
    sqlite3* db = nullptr;
    if(sqlite3_open(dbPath_.c_str(), &db) == SQLITE_OK) {
      sqlite3_stmt* stmt = nullptr;
      const char* ftsSql = scope == SearchScope::Title
        ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts.title MATCH ? ORDER BY rank LIMIT 200;"
        : scope == SearchScope::Content
          ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts.body MATCH ? ORDER BY rank LIMIT 200;"
          : "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.id = notes_fts.id WHERE notes_fts MATCH ? ORDER BY rank LIMIT 200;";
      if(sqlite3_prepare_v2(db, ftsSql, -1, &stmt, nullptr) == SQLITE_OK) {
        const std::string q(query);
        bindText(stmt, 1, q);
        collectRows(stmt, out, query);
      }
      if(stmt) sqlite3_finalize(stmt);
      if(out.empty()) {
        stmt = nullptr;
        const char* likeSql = scope == SearchScope::Title
          ? "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ? ORDER BY title LIMIT 200;"
          : scope == SearchScope::Content
            ? "SELECT id,path,title,body FROM notes WHERE lower(body) LIKE ? ORDER BY title LIMIT 200;"
            : "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ? OR lower(body) LIKE ? OR lower(path) LIKE ? ORDER BY title LIMIT 200;";
        if(sqlite3_prepare_v2(db, likeSql, -1, &stmt, nullptr) == SQLITE_OK) {
          std::string q = lowerCopy(std::string(query));
          q = "%" + q + "%";
          bindText(stmt, 1, q);
          if(scope == SearchScope::All) {
            bindText(stmt, 2, q);
            bindText(stmt, 3, q);
          }
          collectRows(stmt, out, query);
        }
        if(stmt) sqlite3_finalize(stmt);
      }
      sqlite3_close(db);
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
