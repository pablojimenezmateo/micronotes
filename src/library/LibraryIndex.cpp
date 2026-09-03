#include "CoreAliases.h"
#include "library/LibraryIndex.h"

#include "ui/WikiLink.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/AppIdentity.h"
#include "core/persistence/SqliteDb.h"
#include "core/perf/PerformanceCounters.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace micronotes::library {

using persistence::SqliteDb;
using persistence::Statement;

namespace {

// Writes one row per wikilink the note carries. Duplicates collapse on the
// primary key: a note that names the same target three times is one backlink,
// and the first line it appeared on is the one worth showing.
static void recordLinks(sqlite3_stmt* stmt, const std::string& noteId, std::string_view body);

static void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

static std::string columnText(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

// Tags round-trip through one column as a space-separated list. A tag cannot
// hold whitespace -- `ui::splitTags` is what parses one, and it splits on it --
// so this is lossless, and it keeps the row a row rather than a second table
// joined on every note list.
static std::string joinTagList(const std::vector<std::string>& tags) {
  std::string out;
  for(const auto& tag : tags) {
    if(!out.empty()) out.push_back(' ');
    out += tag;
  }
  return out;
}

static std::vector<std::string> splitTagList(std::string_view value) {
  std::vector<std::string> tags;
  std::size_t i = 0;
  while(i < value.size()) {
    while(i < value.size() && value[i] == ' ') ++i;
    const std::size_t start = i;
    while(i < value.size() && value[i] != ' ') ++i;
    if(i > start) tags.emplace_back(value.substr(start, i - start));
  }
  return tags;
}

static std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

// As many snippets as anything downstream will draw, and not one more. The
// sidebar shows three lines per result; a query matching a thousand lines of
// one note used to build a thousand snippets -- three strings each -- and throw
// all but three away, once per note, on every query.
static constexpr std::size_t kMaxSnippets = 3;

// A `LIKE` pattern matching `query` anywhere, with LIKE's own wildcards taken
// literally.
//
// `%` and `_` mean "anything" to LIKE, so a query carrying one used to match
// notes that do not contain the query at all -- "a%t" matched every note with
// an `a` somewhere before a `t` -- and each of those rows then drew a title
// with nothing under it, because the snippet beneath a result is found by a
// literal search of the body and there was nothing literal there to find. The
// escape character has to be escaped too, or a query ending in a backslash
// would escape the pattern's own closing wildcard.
static std::string likePattern(std::string_view query) {
  std::string out = "%";
  for(const char c : lowerCopy(std::string(query))) {
    if(c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out += "%";
  return out;
}

// Case-insensitive `find`, over views. `query` is already lowered.
static std::size_t findLowered(std::string_view line, std::string_view lowerQuery) {
  if(lowerQuery.empty() || line.size() < lowerQuery.size()) return std::string_view::npos;
  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  const std::size_t last = line.size() - lowerQuery.size();
  for(std::size_t at = 0; at <= last; ++at) {
    std::size_t i = 0;
    while(i < lowerQuery.size() && lower(line[at + i]) == lowerQuery[i]) ++i;
    if(i == lowerQuery.size()) return at;
  }
  return std::string_view::npos;
}

// The line `body` holds at `[from, ...)`, and where the one after it starts.
// The same partition `std::getline` produced: the newline belongs to neither
// line, and a buffer ending in one does not yield an empty line after it.
static std::string_view lineAt(std::string_view body, std::size_t from, std::size_t* next) {
  const auto end = body.find('\n', from);
  if(end == std::string_view::npos) {
    *next = body.size();
    return body.substr(from);
  }
  *next = end + 1;
  return body.substr(from, end - from);
}

// Walks `body` a line at a time as views, and materialises a string only for
// the at most three snippets it keeps.
//
// This used to copy the whole body into an `istringstream`, then allocate a
// `std::string` per line of it into a vector, then allocate a lowered copy of
// each of those to search it -- three allocations per line of every note in the
// result set, to show three lines of one. On a query returning 200 notes that
// was the whole cost of the query. Nothing is allocated here per line, and the
// walk stops as soon as the third snippet has the line under it.
static void fillSnippet(SearchResult& result, std::string_view body, std::string_view query) {
  if(query.empty()) return;
  const auto lowerQuery = lowerCopy(std::string(query));

  constexpr std::size_t kNone = static_cast<std::size_t>(-1);
  std::string_view previous;      // the line above the one being tested
  std::size_t awaiting = kNone;   // a snippet still missing the line below it
  std::size_t at = 0;
  while(at < body.size()) {
    std::size_t next = 0;
    const std::string_view line = lineAt(body, at, &next);
    at = next;
    if(awaiting != kNone) {
      result.snippets[awaiting].afterLine = std::string(line);
      awaiting = kNone;
    }
    if(result.snippets.size() < kMaxSnippets) {
      const auto found = findLowered(line, lowerQuery);
      if(found != std::string_view::npos) {
        SearchResult::Snippet snippet;
        snippet.beforeLine = std::string(previous);
        snippet.matchLine = std::string(line);
        // Byte offsets into the line as written. Lowercasing is one-for-one
        // over the bytes this comparison can match -- ASCII letters -- so the
        // offset found against the lowered query addresses the same bytes in
        // the original.
        snippet.matchStart = found;
        snippet.matchLength = lowerQuery.size();
        result.snippets.push_back(std::move(snippet));
        awaiting = result.snippets.size() - 1;
      }
    }
    previous = line;
    if(awaiting == kNone && result.snippets.size() >= kMaxSnippets) break;
  }
  if(!result.snippets.empty()) {
    result.beforeLine = result.snippets.front().beforeLine;
    result.matchLine = result.snippets.front().matchLine;
    result.afterLine = result.snippets.front().afterLine;
    result.matchStart = result.snippets.front().matchStart;
    result.matchLength = result.snippets.front().matchLength;
  }
}

static void collectRows(sqlite3_stmt* stmt, std::vector<SearchResult>& out, std::string_view query) {
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    // Through `columnText`, which is the same read the refresh uses. Building a
    // `std::string` straight from `sqlite3_column_text` is a construction from
    // `nullptr` on a NULL column: unreachable today -- every column read here is
    // declared NOT NULL or comes through an inner join -- but the two spellings
    // sat four functions apart in this file and only one of them was safe.
    out.push_back({columnText(stmt, 0), {}, columnText(stmt, 2)});
    out.back().path = columnText(stmt, 1);
    if(const auto body = columnText(stmt, 3); !body.empty()) {
      fillSnippet(out.back(), body, query);
    }
  }
}

void recordLinks(sqlite3_stmt* stmt, const std::string& noteId, std::string_view body) {
  if(!stmt) return;
  for(const auto& reference : ui::wikiReferences(body)) {
    const auto target = ui::splitWikiTarget(reference.target).note;
    if(target.empty()) continue;
    sqlite3_reset(stmt);
    bindText(stmt, 1, noteId);
    bindText(stmt, 2, target);
    bindText(stmt, 3, reference.line);
    sqlite3_step(stmt);
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

// The shape the code below assumes. Bumped when that shape changes, because a
// library on disk outlives the build that wrote it and an index that is subtly
// the wrong shape returns subtly wrong search results rather than failing.
//
// 2: `notes_fts` is addressed by rowid, and the rowid it carries is the note
//    row's own. Version 1 filed it under an auto-assigned rowid and carried a
//    redundant `id` column, so the fts row for a note could only be found by
//    scanning the whole index.
// 3: `notes` carries `tags` and `icon`. Those two fields are the only things
//    the sidebar's note list needed that the index did not already hold, and
//    without them the note list was a second recursive walk of the library and
//    a re-open plus a front-matter parse of every note in it -- immediately
//    after the refresh had read the same files for the same fields. Two
//    columns turn that into a `SELECT`.
static constexpr int kSchemaVersion = 3;

bool LibraryIndex::migrate() {
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  // No PRAGMA statements here: journal_mode, synchronous and foreign_keys are
  // applied by SqliteDb::open, because the latter two are per-connection and
  // setting them during migration configured only migration's own connection.
  int version = 0;
  if(Statement versionStmt = db.prepare("PRAGMA user_version;"); versionStmt) {
    if(sqlite3_step(versionStmt) == SQLITE_ROW) version = sqlite3_column_int(versionStmt, 0);
  }

  // The index is a cache of what is on disk, so an older shape is dropped and
  // refilled rather than migrated. `refreshChangedFiles` then finds every file
  // changed and re-reads each one exactly once, which is what migrating the
  // rows would have cost anyway -- and there is no second code path holding an
  // older schema's assumptions about the rows it is reading.
  bool ok = true;
  if(version < kSchemaVersion) {
    ok = db.exec("DROP TABLE IF EXISTS notes_fts;") && db.exec("DROP TABLE IF EXISTS notes;") &&
         db.exec("DROP TABLE IF EXISTS links;");
  }
  ok = ok &&
    db.exec("CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, path TEXT NOT NULL, title TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, tags TEXT NOT NULL, icon TEXT NOT NULL, body TEXT NOT NULL);") &&
    // One row per (note, target it names). `target` is the text as written --
    // resolution happens at query time so a rename does not invalidate rows.
    db.exec("CREATE TABLE IF NOT EXISTS links(src_id TEXT NOT NULL, target TEXT NOT NULL, line TEXT NOT NULL, PRIMARY KEY(src_id, target));") &&
    db.exec("CREATE INDEX IF NOT EXISTS links_target ON links(target COLLATE NOCASE);") &&
    db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(title, body, path);");
  if(ok && version < kSchemaVersion) {
    ok = db.exec("PRAGMA user_version=" + std::to_string(kSchemaVersion) + ";");
  }
  return ok;
}

bool LibraryIndex::rebuild() {
  perf::ScopeTimer timer("library_index.rebuild");
  perf::addCounter(perf::CounterId::LibraryIndexRebuilds);
  rows_.clear();
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  if(!db.exec("BEGIN IMMEDIATE; DELETE FROM notes; DELETE FROM notes_fts; DELETE FROM links;")) {
    return false;
  }

  Statement noteStmt = db.prepare("INSERT INTO notes(id,path,title,mtime,size,tags,icon,body) VALUES(?,?,?,?,?,?,?,?);");
  Statement ftsStmt = db.prepare("INSERT INTO notes_fts(rowid,title,body,path) VALUES(?,?,?,?);");
  Statement linkStmt = db.prepare("INSERT OR REPLACE INTO links(src_id,target,line) VALUES(?,?,?);");

  Library library(root_);
  std::vector<std::filesystem::directory_entry> entries;
  library.walk(&entries, &directories_);
  for(const auto& entry : entries) {
    const auto& path = entry.path();
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
    bindText(noteStmt, 6, joinTagList(note.metadata.tags));
    bindText(noteStmt, 7, note.metadata.icon);
    bindText(noteStmt, 8, note.body);
    sqlite3_step(noteStmt);

    // The fts row takes the note row's own rowid. See `refreshChangedFiles`:
    // it is the only address an fts5 table can be deleted by in less than a
    // full scan of it.
    sqlite3_reset(ftsStmt);
    sqlite3_bind_int64(ftsStmt, 1, sqlite3_last_insert_rowid(db.handle()));
    bindText(ftsStmt, 2, title);
    bindText(ftsStmt, 3, note.body);
    bindText(ftsStmt, 4, relative);
    sqlite3_step(ftsStmt);

    recordLinks(linkStmt, noteId, note.body);

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
    // The note row's rowid, which is also the rowid its `notes_fts` row is
    // filed under. Carried here so a removal can address the fts row directly.
    sqlite3_int64 rowId = 0;
  };
  std::unordered_map<std::string, ExistingRow> existing;
  {
  perf::ScopeTimer loadTimer("library_index.refresh.load_existing");
  if(Statement selectStmt = db.prepare("SELECT path,id,mtime,size,rowid FROM notes;"); selectStmt) {
    while(sqlite3_step(selectStmt) == SQLITE_ROW) {
      existing.emplace(columnText(selectStmt, 0), ExistingRow {
        columnText(selectStmt, 1),
        sqlite3_column_int64(selectStmt, 2),
        sqlite3_column_int64(selectStmt, 3),
        sqlite3_column_int64(selectStmt, 4),
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
  std::vector<std::filesystem::directory_entry> entries;
  library.walk(&entries, &directories_);
  for(const auto& entry : entries) {
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

  struct RemovedRow {
    std::string relative;
    std::string id;
    sqlite3_int64 rowId = 0;
  };
  std::vector<RemovedRow> removed;
  for(const auto& [path, row] : existing) {
    if(seenPaths.contains(path)) continue;
    removed.push_back({path, row.id, row.rowId});
  }

  bool ok = true;
  const bool hasWork = !changed.empty() || !removed.empty();

  // Pass two: apply, in one transaction, only if there is anything to apply.
  if(hasWork) {
    if(!db.exec("BEGIN IMMEDIATE;")) return false;

    Statement upsertStmt = db.prepare("INSERT INTO notes(id,path,title,mtime,size,tags,icon,body) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,title=excluded.title,mtime=excluded.mtime,size=excluded.size,tags=excluded.tags,icon=excluded.icon,body=excluded.body;");
    // The fts row is addressed by rowid, and the rowid it carries is the note
    // row's own. `id` in `notes_fts` is UNINDEXED -- fts5 stores it and builds
    // no index over it -- so `DELETE ... WHERE id=?` was a full scan of the
    // whole index, once per changed file. Indexing the first thousand notes of
    // a library was therefore quadratic in the library, and it was 78% of the
    // refresh.
    Statement rowidStmt = db.prepare("SELECT rowid FROM notes WHERE id=?;");
    Statement deleteFtsStmt = db.prepare("DELETE FROM notes_fts WHERE rowid=?;");
    Statement insertFtsStmt = db.prepare("INSERT INTO notes_fts(rowid,title,body,path) VALUES(?,?,?,?);");
    Statement deleteNoteStmt = db.prepare("DELETE FROM notes WHERE path=?;");
    Statement deleteLinksStmt = db.prepare("DELETE FROM links WHERE src_id=?;");
    Statement insertLinkStmt = db.prepare("INSERT OR REPLACE INTO links(src_id,target,line) VALUES(?,?,?);");
    ok = upsertStmt && rowidStmt && deleteFtsStmt && insertFtsStmt && deleteNoteStmt &&
         deleteLinksStmt && insertLinkStmt;

    if(ok) {
      for(const auto& file : changed) {
        perf::addCounter(perf::CounterId::LibraryIndexFilesReread);
        LoadedNote note;
        {
          perf::ScopeTimer readTimer("library_index.refresh.read_file");
          note = library.loadNote(file.path);
        }
        perf::ScopeTimer writeTimer("library_index.refresh.write_rows");
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
        bindText(upsertStmt, 6, joinTagList(note.metadata.tags));
        bindText(upsertStmt, 7, note.metadata.icon);
        bindText(upsertStmt, 8, note.body);
        ok = sqlite3_step(upsertStmt) == SQLITE_DONE;
        if(!ok) break;

        // Which row the upsert wrote. An upsert that updated does not move
        // `last_insert_rowid`, so this is asked rather than assumed -- one
        // lookup through the unique index on `id`.
        sqlite3_reset(rowidStmt);
        bindText(rowidStmt, 1, noteId);
        if(sqlite3_step(rowidStmt) != SQLITE_ROW) {
          ok = false;
          break;
        }
        const sqlite3_int64 rowId = sqlite3_column_int64(rowidStmt, 0);

        sqlite3_reset(deleteFtsStmt);
        sqlite3_bind_int64(deleteFtsStmt, 1, rowId);
        ok = sqlite3_step(deleteFtsStmt) == SQLITE_DONE;
        if(!ok) break;

        sqlite3_reset(insertFtsStmt);
        sqlite3_bind_int64(insertFtsStmt, 1, rowId);
        bindText(insertFtsStmt, 2, title);
        bindText(insertFtsStmt, 3, note.body);
        bindText(insertFtsStmt, 4, file.relative);
        ok = sqlite3_step(insertFtsStmt) == SQLITE_DONE;
        if(!ok) break;

        // Rewritten wholesale rather than diffed: a note's links are however
        // many it has, and working out which ones changed costs more than
        // writing them all again.
        sqlite3_reset(deleteLinksStmt);
        bindText(deleteLinksStmt, 1, noteId);
        ok = sqlite3_step(deleteLinksStmt) == SQLITE_DONE;
        if(!ok) break;
        {
          perf::ScopeTimer linkTimer("library_index.refresh.record_links");
          recordLinks(insertLinkStmt, noteId, note.body);
        }
      }
    }

    if(ok) {
      for(const auto& row : removed) {
        // A note that moved keeps its id under a new path; the upsert above
        // already rewrote the row, so deleting by the old path would drop it.
        if(seenIds.contains(row.id)) continue;
        perf::addCounter(perf::CounterId::LibraryIndexRowsDeleted);
        sqlite3_reset(deleteNoteStmt);
        bindText(deleteNoteStmt, 1, row.relative);
        ok = sqlite3_step(deleteNoteStmt) == SQLITE_DONE;
        if(!ok) break;
        sqlite3_reset(deleteLinksStmt);
        bindText(deleteLinksStmt, 1, row.id);
        sqlite3_step(deleteLinksStmt);
        sqlite3_reset(deleteFtsStmt);
        sqlite3_bind_int64(deleteFtsStmt, 1, row.rowId);
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
        ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts.title MATCH ? ORDER BY rank LIMIT 200;"
        : scope == SearchScope::Content
          ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts.body MATCH ? ORDER BY rank LIMIT 200;"
          : "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts MATCH ? ORDER BY rank LIMIT 200;";
      if(Statement stmt = db.prepare(ftsSql); stmt) {
        const std::string q(query);
        bindText(stmt, 1, q);
        collectRows(stmt, out, query);
      }
      if(out.empty()) {
        const char* likeSql = scope == SearchScope::Title
          ? "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ? ESCAPE '\\' ORDER BY title LIMIT 200;"
          : scope == SearchScope::Content
            ? "SELECT id,path,title,body FROM notes WHERE lower(body) LIKE ? ESCAPE '\\' ORDER BY title LIMIT 200;"
            : "SELECT id,path,title,body FROM notes WHERE lower(title) LIKE ?1 ESCAPE '\\' OR lower(body) LIKE ?2 ESCAPE '\\' OR lower(path) LIKE ?3 ESCAPE '\\' ORDER BY title LIMIT 200;";
        if(Statement stmt = db.prepare(likeSql); stmt) {
          const std::string q = likePattern(query);
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

std::vector<Backlink> LibraryIndex::backlinks(std::string_view title, std::string_view stem) const {
  std::vector<Backlink> found;
  if(!db_.isOpen() || (title.empty() && stem.empty())) return found;
  // NOCASE so a link written in a different case still counts, which is the
  // same latitude resolveWikiLink gives it.
  Statement stmt = db_.prepare(
    "SELECT notes.id, notes.path, notes.title, links.line FROM links "
    "JOIN notes ON notes.id = links.src_id "
    "WHERE links.target = ?1 COLLATE NOCASE OR links.target = ?2 COLLATE NOCASE "
    "ORDER BY notes.title;");
  if(!stmt) return found;
  bindText(stmt, 1, std::string(title));
  bindText(stmt, 2, std::string(stem));
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    found.push_back({columnText(stmt, 0), std::filesystem::path(columnText(stmt, 1)),
                     columnText(stmt, 2), columnText(stmt, 3)});
  }
  return found;
}

std::size_t LibraryIndex::size() const {
  return rows_.size();
}

bool LibraryIndex::isOpen() const {
  return db_.isOpen();
}

std::vector<IndexedNote> LibraryIndex::notes() const {
  perf::ScopeTimer timer("library_index.notes");
  std::vector<IndexedNote> notes;
  if(!db_.isOpen()) return notes;
  Statement stmt = db_.prepare("SELECT id,path,title,tags,icon FROM notes;");
  if(!stmt) return notes;
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    perf::addCounter(perf::CounterId::LibraryNoteRowsSelected);
    notes.push_back(IndexedNote {
      columnText(stmt, 0),
      std::filesystem::path(columnText(stmt, 1)),
      columnText(stmt, 2),
      splitTagList(columnText(stmt, 3)),
      columnText(stmt, 4),
    });
  }
  return notes;
}

const std::vector<std::filesystem::path>& LibraryIndex::directories() const {
  return directories_;
}

}
