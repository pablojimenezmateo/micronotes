#include "CoreAliases.h"
#include "library/LibraryIndex.h"

#include "core/util/StringUtil.h"

#include "doc/WikiLink.h"
#include "library/WikiResolve.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/AppIdentity.h"
#include "core/persistence/SqliteDb.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/DurableFile.h"

#include <sqlite3.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace micronotes::library {

using persistence::SqliteDb;
using persistence::Statement;

// One note as every table here holds it.
//
// The body is either ours or the caller's, and `body()` says which without a
// copy either way. A save already holds the bytes it wrote and they outlive the
// transaction, so handing sqlite a view of them saves two copies of a whole
// note per save: one into this row, and one more inside sqlite.
//
// `body()` rather than a `string_view` member, which is what the first attempt
// used and what made the search tests fail: a row is *move-assigned* into place
// by both refreshes, and a view of our own `owned` string dangles the moment
// that string moves. Deriving it on read cannot get that wrong.
//
// At namespace scope rather than in the anonymous namespace below, because
// `DeferredNoteWrite` holds one and that type is named in the header. A member
// with internal linkage inside one with external linkage is what
// `-Wsubobject-linkage` is about.
struct NoteRow {
  std::string id;
  std::string relative;   // library-relative, generic form; the `path` column
  std::string title;
  std::string tags;       // joined, space separated
  std::string icon;
  long long mtime = 0;
  long long size = 0;
  // Exactly one of these carries the body, and `lends` says which.
  std::string owned;      // the read path's own copy
  std::string_view lent;  // the write path's view of the caller's buffer
  bool lends = false;

  std::string_view body() const { return lends ? lent : std::string_view(owned); }
  void ownBody(std::string text) {
    owned = std::move(text);
    lends = false;
  }
  void lendBody(std::string_view text) {
    lent = text;
    lends = true;
  }
};

// One note's index write, taken by a save and not yet run.
//
// It carries the row to write *and* what sqlite said about that note before the
// first save that deferred -- which is not the same thing as what the row says
// now, and both are needed. The row is what lands; the prior id and rowid are
// what the landing has to clean up after, because a note whose front-matter id
// changed leaves its old row behind under the same path.
struct DeferredNoteWrite {
  NoteRow row;
  bool hadRow = false;
  std::string priorId;
  sqlite3_int64 priorRowId = 0;
};

namespace {

// Writes one row per wikilink the note carries. Duplicates collapse on the
// primary key: a note that names the same target three times is one backlink,
// and the first line it appeared on is the one worth showing.
static void recordLinks(sqlite3_stmt* stmt, const std::string& noteId, std::string_view body);

// Tags round-trip through one column as a space-separated list. A tag cannot
// hold whitespace -- `library::splitTags` is what parses one, and it splits on it --
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

void recordLinks(sqlite3_stmt* stmt, const std::string& noteId, std::string_view body) {
  if(!stmt) return;
  for(const auto& reference : doc::wikiReferences(body)) {
    const auto target = doc::splitWikiTarget(reference.target).note;
    if(target.empty()) continue;
    sqlite3_reset(stmt);
    persistence::bindText(stmt, 1, noteId);
    persistence::bindText(stmt, 2, target);
    persistence::bindText(stmt, 3, reference.line);
    sqlite3_step(stmt);
  }
}

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
  row.ownBody(std::move(note.body));
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
    perf::addCounter(perf::CounterId::LibraryIndexBodyBytesStored, row.body().size());
    sqlite3_reset(upsert_);
    persistence::bindText(upsert_, 1, row.id);
    persistence::bindText(upsert_, 2, row.relative);
    persistence::bindText(upsert_, 3, row.title);
    sqlite3_bind_int64(upsert_, 4, static_cast<sqlite3_int64>(row.mtime));
    sqlite3_bind_int64(upsert_, 5, static_cast<sqlite3_int64>(row.size));
    persistence::bindText(upsert_, 6, row.tags);
    persistence::bindText(upsert_, 7, row.icon);
    persistence::bindTextBorrowed(upsert_, 8, row.body());
    if(sqlite3_step(upsert_) != SQLITE_DONE) return false;

    sqlite3_int64 rowId = 0;
    if(intoEmptyTables) {
      rowId = sqlite3_last_insert_rowid(db_.handle());
    } else {
      // Asked rather than assumed: an upsert that *updated* does not move
      // `last_insert_rowid`. One lookup through the unique index on `id`.
      sqlite3_reset(rowId_);
      persistence::bindText(rowId_, 1, row.id);
      if(sqlite3_step(rowId_) != SQLITE_ROW) return false;
      rowId = sqlite3_column_int64(rowId_, 0);
      sqlite3_reset(deleteFts_);
      sqlite3_bind_int64(deleteFts_, 1, rowId);
      if(sqlite3_step(deleteFts_) != SQLITE_DONE) return false;
    }

    {
      // Its own timer, because it is the half of a write that scales with the
      // *note* rather than with the row: the notes upsert stores the body and
      // fts5 tokenises it, and a save runs once a second while somebody types.
      // Without the split the two are one number and the number is a mean over
      // a thousand small notes and one large one.
      perf::ScopeTimer ftsTimer("library_index.write_fts");
      perf::addCounter(perf::CounterId::LibraryIndexBodyBytesIndexed, row.body().size());
      sqlite3_reset(insertFts_);
      sqlite3_bind_int64(insertFts_, 1, rowId);
      persistence::bindText(insertFts_, 2, row.title);
      persistence::bindTextBorrowed(insertFts_, 3, row.body());
      persistence::bindText(insertFts_, 4, row.relative);
      if(sqlite3_step(insertFts_) != SQLITE_DONE) return false;
    }

    // Rewritten wholesale rather than diffed: a note's links are however many
    // it has, and working out which of them changed costs more than writing
    // them all again.
    if(!intoEmptyTables) {
      sqlite3_reset(deleteLinks_);
      persistence::bindText(deleteLinks_, 1, row.id);
      if(sqlite3_step(deleteLinks_) != SQLITE_DONE) return false;
    }
    perf::ScopeTimer linkTimer("library_index.record_links");
    recordLinks(insertLink_, row.id, row.body());
    return true;
  }

  // Drops every trace of the note filed at `relative`. `ftsRowId` is the note
  // row's own rowid, carried by the caller because the row is about to go and
  // the fts table cannot be addressed without it.
  bool erase(const std::string& relative, const std::string& id, sqlite3_int64 ftsRowId) {
    perf::addCounter(perf::CounterId::LibraryIndexRowsDeleted);
    sqlite3_reset(deleteNote_);
    persistence::bindText(deleteNote_, 1, relative);
    if(sqlite3_step(deleteNote_) != SQLITE_DONE) return false;
    sqlite3_reset(deleteLinks_);
    persistence::bindText(deleteLinks_, 1, id);
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

LibraryIndex::LibraryIndex() = default;

LibraryIndex::~LibraryIndex() {
  // The last save's write, run on the way out. Losing it would lose no data --
  // the note's own file is written durably *before* the index is told about it
  // -- but the next start would have to read that file back to discover as
  // much, and there is no reason to make it.
  flushDeferred();
}

DeferredNoteWrite* LibraryIndex::deferredFor(std::string_view relative) const {
  if(!deferred_) return nullptr;
  return deferred_->row.relative == relative ? deferred_.get() : nullptr;
}

// The half of a save that scales with the note, run now.
//
// Every read of these tables calls this first, which is what makes "current
// before every read" a property of the class rather than a rule its callers
// have to remember -- see the header, and TD-48's entry for the four years the
// difference cost.
void LibraryIndex::flushDeferred() const {
  if(!deferred_) return;
  // Taken before it is attempted. A write that fails must not stay standing to
  // be retried by every read after it: the file on disk is the truth, its stat
  // no longer matches the row, and the next refresh of that file will index it
  // again for the ordinary reason.
  const std::unique_ptr<DeferredNoteWrite> pending = std::move(deferred_);
  if(!db_.isOpen()) return;
  perf::ScopeTimer timer("library_index.flush_deferred");
  perf::addCounter(perf::CounterId::LibraryIndexFlushes);
  SqliteDb& db = db_;
  if(!db.exec("BEGIN IMMEDIATE;")) return;
  NoteWriter writer(db);
  bool ok = writer.ready() && writer.write(pending->row, /*intoEmptyTables=*/false);
  // A note whose front-matter id changed left its old row behind under the same
  // path -- the upsert keys on id, so it inserted rather than updated. The id
  // and rowid are the ones sqlite held before the *first* save that deferred,
  // which is the row that is actually still there.
  if(ok && pending->hadRow && pending->priorId != pending->row.id) {
    ok = writer.erase(pending->row.relative, pending->priorId, pending->priorRowId);
  }
  if(!ok || !db.exec("COMMIT;")) db.exec("ROLLBACK;");
}

bool LibraryIndex::open(const std::filesystem::path& libraryRoot) {
  // Whatever the library being left had taken, before the connection under it
  // is replaced. A deferred write is addressed by a library-relative path and
  // means nothing against another root.
  flushDeferred();
  root_ = libraryRoot;
  std::filesystem::create_directories(root_ / microcore::kAppDotDir);
  dbPath_ = root_ / microcore::kAppDotDir / "index.sqlite";
  // The one connection every later call reuses. SqliteDb::open applies the
  // per-connection pragmas, which is the only place they can take effect.
  if(!db_.open(dbPath_)) return false;
  return migrate();
}


bool LibraryIndex::rebuild() {
  perf::ScopeTimer timer("library_index.rebuild");
  perf::addCounter(perf::CounterId::LibraryIndexRebuilds);
  // Dropped rather than flushed, and this is the one place that is the right
  // answer: a rebuild empties the three tables and reads every note off the
  // disk again, and the note a save took a write for was written durably to
  // that disk before the index was told. Running the write first would put a
  // row into a table this line is about to delete.
  deferred_.reset();
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
  library.walk(&entries, &directories_, &companions_);
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

  // A write taken for this same note is the one thing that does not have to be
  // run first -- it is about to be replaced. One taken for any *other* note is
  // in the way: the statements below read and write the same tables, and they
  // have to see what the last save said.
  DeferredNoteWrite* standing = deferredFor(relative);
  if(!standing) flushDeferred();

  // The row as it stands, by path. `notes_path` is what makes this a lookup
  // rather than a scan of the whole table. Read only when nothing is standing:
  // a deferred write already *is* the answer to this query, one memcpy away,
  // and asking sqlite would get the row underneath it -- which would report
  // every field of the last save as changed all over again, and would fail the
  // stat check the last save has already passed.
  NoteRow stored;
  bool hadStoredRow = false;
  sqlite3_int64 storedRowId = 0;
  if(!standing) {
    if(Statement stmt =
         db.prepare("SELECT id,title,tags,icon,mtime,size,rowid FROM notes WHERE path=?;");
       stmt) {
      persistence::bindText(stmt, 1, relative);
      if(sqlite3_step(stmt) == SQLITE_ROW) {
        hadStoredRow = true;
        stored.id = persistence::columnText(stmt, 0);
        stored.title = persistence::columnText(stmt, 1);
        stored.tags = persistence::columnText(stmt, 2);
        stored.icon = persistence::columnText(stmt, 3);
        stored.mtime = sqlite3_column_int64(stmt, 4);
        stored.size = sqlite3_column_int64(stmt, 5);
        stored.relative = relative;
        storedRowId = sqlite3_column_int64(stmt, 6);
      }
    }
  }

  // Two different "before"s, and conflating them is the bug this split exists
  // to avoid. `before` is what the tables will say once everything taken has
  // run, and is what a change is measured against. `prior*` is what sqlite
  // actually holds right now, and is the only thing an `erase` can address.
  const NoteRow& before = standing ? standing->row : stored;
  const bool knownBefore = standing != nullptr || hadStoredRow;
  const bool hadPriorRow = standing ? standing->hadRow : hadStoredRow;
  const std::string priorId = standing ? standing->priorId : stored.id;
  const sqlite3_int64 priorRowId = standing ? standing->priorRowId : storedRowId;

  const auto disk = platform::statFile(absolutePath);
  if(!disk.exists) {
    // Gone. Nothing to do unless the index still thinks it is there, which is
    // what a delete and the vacated end of a move both look like. A write taken
    // for a file that no longer exists is dropped rather than run: writing the
    // row back and erasing it again in one transaction is the same end state
    // for twice the work.
    deferred_.reset();
    if(!knownBefore) {
      result.ok = true;
      return result;
    }
    if(!hadPriorRow) {
      // Never reached sqlite at all: the note was created and deleted between
      // two reads. Nothing to erase, and the note list still has to be told.
      result.ok = true;
      result.listFieldsChanged = true;
      result.rowsWritten = true;
      return result;
    }
    if(!db.exec("BEGIN IMMEDIATE;")) return result;
    NoteWriter writer(db);
    const bool ok = writer.ready() && writer.erase(relative, priorId, priorRowId);
    result.ok = ok && db.exec("COMMIT;");
    if(!result.ok) db.exec("ROLLBACK;");
    // A row that went away is a row the note list was showing.
    result.listFieldsChanged = result.ok;
    result.rowsWritten = result.ok;
    return result;
  }

  // Unchanged since the row was written: this is the common answer when the
  // watcher reports our own write back to us, and it costs one stat.
  if(knownBefore && before.mtime == static_cast<long long>(disk.mtimeNanos) &&
     before.size == static_cast<long long>(disk.size)) {
    result.ok = true;
    return result;
  }

  if(written) {
    NoteRow row =
      noteRowFrom(*written->metadata, absolutePath, relative,
                  static_cast<long long>(disk.mtimeNanos), static_cast<long long>(disk.size));
    result.listFieldsChanged = !knownBefore || !sameListFields(before, row);
    result.rowsWritten = true;
    result.ok = true;
    // The one copy this path makes, and the reason it is worth making: the
    // caller lends its buffer for the length of the call and the write is not
    // going to happen inside it. A 200 KB memcpy stands in for storing 200 KB
    // in `notes` and tokenising the same 200 KB into `notes_fts` -- and for
    // fifty-nine saves out of sixty it stands in for them permanently, because
    // the next keystroke's save replaces this row before anything reads it.
    //
    // Into the buffer the coalesced write was already holding, when there is
    // one. Those fifty-nine saves each *allocated* a 200 KB string and freed
    // the 200 KB string of the save they replaced -- a malloc/free pair per
    // second while somebody types, for a note whose length barely moves between
    // two keystrokes. `assign` into capacity that is already the right size is
    // the memcpy without either. Safe because `before` -- which aliases
    // `standing->row` -- has been read on the line above and is not read again.
    std::string buffer;
    if(standing) {
      buffer = std::move(standing->row.owned);
      perf::addCounter(perf::CounterId::LibraryIndexDeferredBodyReused);
    }
    buffer.assign(written->body);
    row.ownBody(std::move(buffer));
    auto pending = std::make_unique<DeferredNoteWrite>();
    pending->row = std::move(row);
    pending->hadRow = hadPriorRow;
    pending->priorId = priorId;
    pending->priorRowId = priorRowId;
    perf::addCounter(perf::CounterId::LibraryIndexWritesDeferred);
    if(standing) perf::addCounter(perf::CounterId::LibraryIndexWritesCoalesced);
    // `standing` -- and `before` with it -- dies here, which is why every
    // answer above was taken before this line.
    deferred_ = std::move(pending);
    return result;
  }

  // Read off the disk, so this is not a save and there is nothing to coalesce
  // with: whatever was taken for this note has been overtaken by the file
  // itself. Written straight through, the way every refresh used to be.
  NoteRow row;
  {
    perf::addCounter(perf::CounterId::LibraryIndexFilesReread);
    perf::ScopeTimer readTimer("library_index.refresh_file.read");
    Library library(root_);
    row = readNoteRow(library, absolutePath, relative, static_cast<long long>(disk.mtimeNanos),
                      static_cast<long long>(disk.size));
  }
  // Before the drop below, which takes `before` with it.
  const bool changed = !knownBefore || !sameListFields(before, row);
  deferred_.reset();

  if(!db.exec("BEGIN IMMEDIATE;")) return result;
  NoteWriter writer(db);
  bool ok = writer.ready() && writer.write(row, /*intoEmptyTables=*/false);
  // A note whose front-matter id changed leaves its old row behind under the
  // same path -- the upsert keys on id, so it inserted rather than updated.
  if(ok && hadPriorRow && priorId != row.id) ok = writer.erase(relative, priorId, priorRowId);
  result.ok = ok && db.exec("COMMIT;");
  if(!result.ok) {
    db.exec("ROLLBACK;");
    return result;
  }
  result.listFieldsChanged = changed;
  result.rowsWritten = true;
  return result;
}

bool LibraryIndex::refreshChangedFiles() {
  // This walk decides what to re-read by comparing each file's stat against its
  // row, and a row a save has taken a write for is behind its file by exactly
  // that write -- so without this the whole point of deferring would be undone
  // here, by re-reading the note off the disk to write what is already held.
  flushDeferred();
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
      existing.emplace(persistence::columnText(selectStmt, 0), ExistingRow {
        persistence::columnText(selectStmt, 1),
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
  library.walk(&entries, &directories_, &companions_);
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

std::size_t LibraryIndex::size() const {
  flushDeferred();
  if(!db_.isOpen()) return 0;
  Statement stmt = db_.prepare("SELECT count(*) FROM notes;");
  if(!stmt || sqlite3_step(stmt) != SQLITE_ROW) return 0;
  return static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
}

bool LibraryIndex::isOpen() const {
  return db_.isOpen();
}

bool LibraryIndex::ftsStoresBodies() const {
  // Reads `sqlite_master` and nothing a save writes, so this one does not need
  // it -- but the rule is worth more than the transaction it saves: every
  // public read of this class flushes, so there is no per-method judgement for
  // the next one to get wrong. A flush with nothing taken is a null check.
  flushDeferred();
  if(!db_.isOpen()) return false;
  // Read off the table's own DDL, not probed by querying it: a contentless
  // table still *declares* every column and answers NULL for it rather than
  // refusing, so a `SELECT body FROM notes_fts` prepares and steps happily on
  // both shapes and tells them apart not at all.
  Statement stmt =
    db_.prepare("SELECT sql FROM sqlite_master WHERE type='table' AND name='notes_fts';");
  if(!stmt || sqlite3_step(stmt) != SQLITE_ROW) return false;
  const auto* sql = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  return sql == nullptr || std::string_view(sql).find("content=''") == std::string_view::npos;
}

std::vector<IndexedNote> LibraryIndex::notes() const {
  perf::ScopeTimer timer("library_index.notes");
  flushDeferred();
  std::vector<IndexedNote> notes;
  if(!db_.isOpen()) return notes;
  Statement stmt = db_.prepare("SELECT id,path,title,tags,icon FROM notes;");
  if(!stmt) return notes;
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    perf::addCounter(perf::CounterId::LibraryNoteRowsSelected);
    notes.push_back(IndexedNote {
      persistence::columnText(stmt, 0),
      std::filesystem::path(persistence::columnText(stmt, 1)),
      persistence::columnText(stmt, 2),
      splitTagList(persistence::columnText(stmt, 3)),
      persistence::columnText(stmt, 4),
    });
  }
  return notes;
}

const std::vector<std::filesystem::path>& LibraryIndex::directories() const {
  return directories_;
}

const std::vector<CompanionEntry>& LibraryIndex::companions() const {
  return companions_;
}

}
