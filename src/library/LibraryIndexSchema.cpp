#include "CoreAliases.h"
#include "library/LibraryIndex.h"

#include "core/persistence/SqliteDb.h"

#include <sqlite3.h>

#include <string>

// The index's shape on disk, and how a library written by an older build is
// brought forward to it.
//
// Its own translation unit because it is a different subject from the rest of
// `LibraryIndex`: everything else in that class is about keeping the tables in
// step with the files, and this is about what the tables *are*. The numbered
// history below is a document -- the five shapes this index has had and what
// each one cost -- and it was sitting in the middle of the refresh machinery
// that reads it.

namespace micronotes::library {

using persistence::SqliteDb;
using persistence::Statement;

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
// 5: `notes_fts` stores no copy of the note. It carried one -- `notes.body` and
//    `notes_fts.body` both held the whole text of every note -- so the index
//    was two and a half times the size of the library it indexed: 27.2 MB for a
//    1,000-note fixture whose Markdown is 10.3 MB. A contentless table stores
//    the terms and not the text, which is all the search has ever needed from
//    it: the snippets are built from `notes.body` through the join, and the
//    only thing asked of `notes_fts` is which rowids match. 27.2 MB to 15.4 MB
//    on the same fixture, and one of the two whole-body writes per save goes
//    with it.
static constexpr int kSchemaVersion = 5;

bool LibraryIndex::migrate() {
  // A migration may drop and rebuild the very tables a save has taken a write
  // for, so the write has to land under the old shape or not at all.
  flushDeferred();
  if(!db_.isOpen()) return false;
  SqliteDb& db = db_;
  // No PRAGMA statements here: journal_mode, synchronous and foreign_keys are
  // applied by SqliteDb::open, because the latter two are per-connection and
  // setting them during migration configured only migration's own connection.
  int version = 0;
  if(Statement versionStmt = db.prepare("PRAGMA user_version;"); versionStmt) {
    if(sqlite3_step(versionStmt) == SQLITE_ROW) version = sqlite3_column_int(versionStmt, 0);
  }

  // The index is a cache of what is on disk, so a shape that is not this one is
  // dropped and refilled rather than migrated. `refreshChangedFiles` then finds
  // every file changed and re-reads each one exactly once, which is what
  // migrating the rows would have cost anyway -- and there is no second code
  // path holding another schema's assumptions about the rows it is reading.
  //
  // `!=` rather than `<`: a library opened by a newer build and then by an
  // older one is an ordinary thing to do -- two machines, or a downgrade -- and
  // the old build would otherwise read a shape it does not know it is reading.
  const bool shapeChanged = version != kSchemaVersion;
  bool ok = true;
  if(shapeChanged) {
    ok = db.exec("DROP TABLE IF EXISTS notes_fts;") && db.exec("DROP TABLE IF EXISTS notes;") &&
         db.exec("DROP TABLE IF EXISTS links;");
  }
  ok = ok &&
    db.exec("CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, path TEXT NOT NULL, title TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, tags TEXT NOT NULL, icon TEXT NOT NULL, body TEXT NOT NULL);") &&
    // One row per (note, target it names). `target` is the text as written --
    // resolution happens at query time so a rename does not invalidate rows.
    db.exec("CREATE TABLE IF NOT EXISTS links(src_id TEXT NOT NULL, target TEXT NOT NULL, line TEXT NOT NULL, PRIMARY KEY(src_id, target));") &&
    db.exec("CREATE INDEX IF NOT EXISTS links_target ON links(target COLLATE NOCASE);") &&
    db.exec("CREATE INDEX IF NOT EXISTS notes_path ON notes(path);");

  // The terms, not the text. `contentless_delete=1` is what makes a contentless
  // table usable here and is SQLite 3.43 or newer: without it such a table
  // cannot have a row deleted, and deleting a row is what every save does --
  // the writer removes the note's fts entry by rowid and inserts the new one.
  //
  // Attempted rather than assumed, because micronotes links the *system*
  // SQLite and a distribution older than 3.43 is an ordinary thing to be on.
  // The fallback is the table every build wrote until now: correct, searchable,
  // and carrying the second copy of every note. Nothing above this line knows
  // which of the two it got, because every statement the index runs against
  // `notes_fts` -- insert by rowid, delete by rowid, delete all, and a MATCH
  // scoped to one column or to none -- means the same thing on both.
  ok = ok && (db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING "
                      "fts5(title, body, path, content='', contentless_delete=1);") ||
              db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING "
                      "fts5(title, body, path);"));
  if(ok && shapeChanged) {
    ok = db.exec("PRAGMA user_version=" + std::to_string(kSchemaVersion) + ";");
  }
  return ok;
}

}
