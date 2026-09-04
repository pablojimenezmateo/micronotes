#include "CoreAliases.h"
#include "library/LibraryIndex.h"

#include "ui/WikiLink.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/AppIdentity.h"
#include "core/persistence/SqliteDb.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/DurableFile.h"

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

// One note as every table here holds it.
struct NoteRow {
  std::string id;
  std::string relative;   // library-relative, generic form; the `path` column
  std::string title;
  std::string tags;       // joined, space separated
  std::string icon;
  long long mtime = 0;
  long long size = 0;
  std::string body;
};

// The five fields the note list is built from. Deliberately not the body and
// not the stat: a save changes both of those and neither is visible in the
// sidebar, the tree, the folder counts or the tag list.
static bool sameListFields(const NoteRow& lhs, const NoteRow& rhs) {
  return lhs.id == rhs.id && lhs.relative == rhs.relative && lhs.title == rhs.title &&
         lhs.tags == rhs.tags && lhs.icon == rhs.icon;
}

// A note's front matter and body in row form. The one place the five list
// fields are derived, so the read path and the just-written path cannot come to
// different answers about the same note.
static NoteRow noteRowFrom(const NoteMetadata& metadata, const std::filesystem::path& path,
                           std::string relative, long long mtime, long long size) {
  NoteRow row;
  // A note written by another tool has no front matter; it is indexed anyway,
  // under an id derived from its path.
  row.id = metadata.id.empty() ? fallbackNoteId(relative) : metadata.id;
  row.title = metadata.title.empty() ? path.stem().string() : metadata.title;
  row.tags = joinTagList(metadata.tags);
  row.icon = metadata.icon;
  row.relative = std::move(relative);
  row.mtime = mtime;
  row.size = size;
  return row;
}

// Reads the note at `path` into row form. The stat is passed in rather than
// taken here, because every caller already has one -- the tree scan from the
// directory_entry it walked, the single-file refresh from the stat it needed to
// decide whether to bother at all.
static NoteRow readNoteRow(const Library& library, const std::filesystem::path& path,
                           std::string relative, long long mtime, long long size) {
  auto note = library.loadNote(path);
  NoteRow row = noteRowFrom(note.metadata, path, std::move(relative), mtime, size);
  row.body = std::move(note.body);
  return row;
}

// The statements one note's rows go through, prepared once and reused.
//
// The full rebuild, the changed-file scan and the single-file refresh all write
// the same three tables in the same order. They used to carry a copy each of
// the same six statements and the same twelve binds, and the copies had already
// drifted: only one of the three went through `recordLinks`, and only one knew
// that an fts5 row can be addressed by rowid. One writer means a fix to the
// order or the binds is a fix everywhere.
class NoteWriter {
public:
  explicit NoteWriter(SqliteDb& db)
    : upsert_(db.prepare("INSERT INTO notes(id,path,title,mtime,size,tags,icon,body) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,title=excluded.title,mtime=excluded.mtime,size=excluded.size,tags=excluded.tags,icon=excluded.icon,body=excluded.body;")),
      // The fts row is addressed by rowid, and the rowid it carries is the note
      // row's own. `notes_fts` has no indexed id column -- fts5 would store one
      // UNINDEXED and build nothing over it -- so a delete by anything else is
      // a full scan of the whole index, once per changed file. That made
      // indexing the first thousand notes quadratic in the library, and it was
      // 78% of the refresh.
      rowId_(db.prepare("SELECT rowid FROM notes WHERE id=?;")),
      deleteFts_(db.prepare("DELETE FROM notes_fts WHERE rowid=?;")),
      insertFts_(db.prepare("INSERT INTO notes_fts(rowid,title,body,path) VALUES(?,?,?,?);")),
      deleteLinks_(db.prepare("DELETE FROM links WHERE src_id=?;")),
      insertLink_(db.prepare("INSERT OR REPLACE INTO links(src_id,target,line) VALUES(?,?,?);")),
      deleteNote_(db.prepare("DELETE FROM notes WHERE path=?;")),
      db_(db) {}

  bool ready() const {
    return upsert_ && rowId_ && deleteFts_ && insertFts_ && deleteLinks_ && insertLink_ &&
           deleteNote_;
  }

  // Writes the note's row, its fts row and its links. `intoEmptyTables` skips
  // the fts delete, which a table that was just emptied cannot need.
  bool write(const NoteRow& row, bool intoEmptyTables) {
    perf::ScopeTimer timer("library_index.write_rows");
    sqlite3_reset(upsert_);
    bindText(upsert_, 1, row.id);
    bindText(upsert_, 2, row.relative);
    bindText(upsert_, 3, row.title);
    sqlite3_bind_int64(upsert_, 4, static_cast<sqlite3_int64>(row.mtime));
    sqlite3_bind_int64(upsert_, 5, static_cast<sqlite3_int64>(row.size));
    bindText(upsert_, 6, row.tags);
    bindText(upsert_, 7, row.icon);
    bindText(upsert_, 8, row.body);
    if(sqlite3_step(upsert_) != SQLITE_DONE) return false;

    sqlite3_int64 rowId = 0;
    if(intoEmptyTables) {
      rowId = sqlite3_last_insert_rowid(db_.handle());
    } else {
      // Asked rather than assumed: an upsert that *updated* does not move
      // `last_insert_rowid`. One lookup through the unique index on `id`.
      sqlite3_reset(rowId_);
      bindText(rowId_, 1, row.id);
      if(sqlite3_step(rowId_) != SQLITE_ROW) return false;
      rowId = sqlite3_column_int64(rowId_, 0);
      sqlite3_reset(deleteFts_);
      sqlite3_bind_int64(deleteFts_, 1, rowId);
      if(sqlite3_step(deleteFts_) != SQLITE_DONE) return false;
    }

    sqlite3_reset(insertFts_);
    sqlite3_bind_int64(insertFts_, 1, rowId);
    bindText(insertFts_, 2, row.title);
    bindText(insertFts_, 3, row.body);
    bindText(insertFts_, 4, row.relative);
    if(sqlite3_step(insertFts_) != SQLITE_DONE) return false;

    // Rewritten wholesale rather than diffed: a note's links are however many
    // it has, and working out which of them changed costs more than writing
    // them all again.
    if(!intoEmptyTables) {
      sqlite3_reset(deleteLinks_);
      bindText(deleteLinks_, 1, row.id);
      if(sqlite3_step(deleteLinks_) != SQLITE_DONE) return false;
    }
    perf::ScopeTimer linkTimer("library_index.record_links");
    recordLinks(insertLink_, row.id, row.body);
    return true;
  }

  // Drops every trace of the note filed at `relative`. `ftsRowId` is the note
  // row's own rowid, carried by the caller because the row is about to go and
  // the fts table cannot be addressed without it.
  bool erase(const std::string& relative, const std::string& id, sqlite3_int64 ftsRowId) {
    perf::addCounter(perf::CounterId::LibraryIndexRowsDeleted);
    sqlite3_reset(deleteNote_);
    bindText(deleteNote_, 1, relative);
    if(sqlite3_step(deleteNote_) != SQLITE_DONE) return false;
    sqlite3_reset(deleteLinks_);
    bindText(deleteLinks_, 1, id);
    sqlite3_step(deleteLinks_);
    sqlite3_reset(deleteFts_);
    sqlite3_bind_int64(deleteFts_, 1, ftsRowId);
    return sqlite3_step(deleteFts_) == SQLITE_DONE;
  }

private:
  Statement upsert_;
  Statement rowId_;
  Statement deleteFts_;
  Statement insertFts_;
  Statement deleteLinks_;
  Statement insertLink_;
  Statement deleteNote_;
  SqliteDb& db_;
};

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
// 4: there is an index on `notes(path)`. `path` is how a single file is found
//    and how a removed row is deleted, and both were a scan of the whole
//    table -- which is the entire point of `refreshFile`, so without this it
//    would have been a scan per save. Deliberately NOT unique: two files
//    exchanging names make the upsert momentarily want one path on two rows,
//    and a uniqueness violation there would roll back the whole refresh rather
//    than leave the duplicate the old code silently allowed.
static constexpr int kSchemaVersion = 4;

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
    db.exec("CREATE INDEX IF NOT EXISTS notes_path ON notes(path);") &&
    db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(title, body, path);");
  if(ok && version < kSchemaVersion) {
    ok = db.exec("PRAGMA user_version=" + std::to_string(kSchemaVersion) + ";");
  }
  return ok;
}

bool LibraryIndex::rebuild() {
  perf::ScopeTimer timer("library_index.rebuild");
  perf::addCounter(perf::CounterId::LibraryIndexRebuilds);
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  if(!db.exec("BEGIN IMMEDIATE; DELETE FROM notes; DELETE FROM notes_fts; DELETE FROM links;")) {
    return false;
  }

  NoteWriter writer(db);
  if(!writer.ready()) {
    db.exec("ROLLBACK;");
    return false;
  }

  Library library(root_);
  std::vector<std::filesystem::directory_entry> entries;
  library.walk(&entries, &directories_);
  bool ok = true;
  for(const auto& entry : entries) {
    const auto& path = entry.path();
    perf::addCounter(perf::CounterId::LibraryIndexFilesScanned);
    std::error_code error;
    const auto mtime = entry.last_write_time(error).time_since_epoch().count();
    const auto size = static_cast<long long>(entry.file_size(error));
    if(error) continue;
    // The tables were emptied above, so every write is an insert: no fts row to
    // delete first, and no links row either.
    ok = writer.write(readNoteRow(library, path, path.lexically_relative(root_).generic_string(),
                                  static_cast<long long>(mtime), size),
                      /*intoEmptyTables=*/true);
    if(!ok) break;
  }

  if(!ok) {
    db.exec("ROLLBACK;");
    return false;
  }
  return db.exec("COMMIT;");
}

LibraryIndex::FileRefresh LibraryIndex::refreshFile(const std::filesystem::path& absolutePath) {
  return refreshPath(absolutePath, nullptr);
}

LibraryIndex::FileRefresh LibraryIndex::refreshWrittenFile(const std::filesystem::path& absolutePath,
                                                           const NoteMetadata& metadata,
                                                           std::string_view body) {
  const WrittenNote written {&metadata, body};
  return refreshPath(absolutePath, &written);
}

LibraryIndex::FileRefresh LibraryIndex::refreshPath(const std::filesystem::path& absolutePath,
                                                    const WrittenNote* written) {
  perf::ScopeTimer timer("library_index.refresh_file");
  perf::addCounter(perf::CounterId::LibraryIndexFileRefreshCalls);
  FileRefresh result;
  if(!db_.isOpen()) return result;
  SqliteDb& db = db_;
  const auto relative = absolutePath.lexically_relative(root_).generic_string();
  if(relative.empty() || relative.starts_with("..")) return result;

  // The row as it stands, by path. `notes_path` is what makes this a lookup
  // rather than a scan of the whole table.
  NoteRow before;
  bool hadRow = false;
  sqlite3_int64 rowId = 0;
  if(Statement stmt = db.prepare("SELECT id,title,tags,icon,mtime,size,rowid FROM notes WHERE path=?;");
     stmt) {
    bindText(stmt, 1, relative);
    if(sqlite3_step(stmt) == SQLITE_ROW) {
      hadRow = true;
      before.id = columnText(stmt, 0);
      before.title = columnText(stmt, 1);
      before.tags = columnText(stmt, 2);
      before.icon = columnText(stmt, 3);
      before.mtime = sqlite3_column_int64(stmt, 4);
      before.size = sqlite3_column_int64(stmt, 5);
      before.relative = relative;
      rowId = sqlite3_column_int64(stmt, 6);
    }
  }

  const auto disk = platform::statFile(absolutePath);
  if(!disk.exists) {
    // Gone. Nothing to do unless the index still thinks it is there, which is
    // what a delete and the vacated end of a move both look like.
    if(!hadRow) {
      result.ok = true;
      return result;
    }
    if(!db.exec("BEGIN IMMEDIATE;")) return result;
    NoteWriter writer(db);
    const bool ok = writer.ready() && writer.erase(relative, before.id, rowId);
    result.ok = ok && db.exec("COMMIT;");
    if(!result.ok) db.exec("ROLLBACK;");
    // A row that went away is a row the note list was showing.
    result.listFieldsChanged = result.ok;
    result.rowsWritten = result.ok;
    return result;
  }

  // Unchanged since the row was written: this is the common answer when the
  // watcher reports our own write back to us, and it costs one stat.
  if(hadRow && before.mtime == static_cast<long long>(disk.mtimeNanos) &&
     before.size == static_cast<long long>(disk.size)) {
    result.ok = true;
    return result;
  }

  NoteRow row;
  if(written) {
    row = noteRowFrom(*written->metadata, absolutePath, relative,
                      static_cast<long long>(disk.mtimeNanos), static_cast<long long>(disk.size));
    // The one copy of the body this path makes, and it is the copy sqlite needs
    // anyway. Nothing is read and nothing is parsed.
    row.body = written->body;
  } else {
    perf::addCounter(perf::CounterId::LibraryIndexFilesReread);
    perf::ScopeTimer readTimer("library_index.refresh_file.read");
    Library library(root_);
    row = readNoteRow(library, absolutePath, relative, static_cast<long long>(disk.mtimeNanos),
                      static_cast<long long>(disk.size));
  }

  if(!db.exec("BEGIN IMMEDIATE;")) return result;
  NoteWriter writer(db);
  bool ok = writer.ready() && writer.write(row, /*intoEmptyTables=*/false);
  // A note whose front-matter id changed leaves its old row behind under the
  // same path -- the upsert keys on id, so it inserted rather than updated.
  if(ok && hadRow && before.id != row.id) ok = writer.erase(relative, before.id, rowId);
  result.ok = ok && db.exec("COMMIT;");
  if(!result.ok) {
    db.exec("ROLLBACK;");
    return result;
  }
  result.listFieldsChanged = !hadRow || !sameListFields(before, row);
  result.rowsWritten = true;
  return result;
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
    changed.push_back(ChangedFile {path, std::move(relative), static_cast<long long>(mtime), size});
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
  // Pass two: apply, in one transaction, only if there is anything to apply.
  if(!changed.empty() || !removed.empty()) {
    if(!db.exec("BEGIN IMMEDIATE;")) return false;
    NoteWriter writer(db);
    ok = writer.ready();

    for(const auto& file : changed) {
      if(!ok) break;
      perf::addCounter(perf::CounterId::LibraryIndexFilesReread);
      NoteRow row;
      {
        perf::ScopeTimer readTimer("library_index.refresh.read_file");
        row = readNoteRow(library, file.path, file.relative, file.mtime, file.size);
      }
      seenIds.insert(row.id);
      ok = writer.write(row, /*intoEmptyTables=*/false);
    }

    for(const auto& row : removed) {
      if(!ok) break;
      // A note that moved keeps its id under a new path; the write above
      // already rewrote the row, so deleting by the old path would drop it.
      if(seenIds.contains(row.id)) continue;
      ok = writer.erase(row.relative, row.id, row.rowId);
    }

    ok = ok && db.exec("COMMIT;");
    if(!ok) db.exec("ROLLBACK;");
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
  // No connection, so no rows. There used to be an in-memory projection of the
  // table here to fall back on -- a thousand `SearchResult`s rebuilt on every
  // refresh that found work, which is every save -- and it was unreachable:
  // nothing filled it unless the database was open, and while the database is
  // open the branch above always returns.
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
  if(!db_.isOpen()) return 0;
  Statement stmt = db_.prepare("SELECT count(*) FROM notes;");
  if(!stmt || sqlite3_step(stmt) != SQLITE_ROW) return 0;
  return static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
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
