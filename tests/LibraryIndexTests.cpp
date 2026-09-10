#include "TestSupport.h"
#include "ScopedXdgDataHome.h"
#include "TempDir.h"

#include <sqlite3.h>

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"
#include "library/NoteCatalog.h"
#include "core/perf/PerformanceCounters.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// Keeping the index in step with the files, which is the half of the index that
// is not about a query: what a rebuild reads, what a refresh discovers, what a
// save already knows, and what the table stores once it gets there.
//
// Split out of `SearchTests.cpp`, which held this beside the query, the links
// table and the organization service.

MICRONOTES_TEST(library_index_searches_file_backed_rows) {
  const micronotes::tests::TempDir rootDir("micronotes-index-file-backed-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "today-note";
  metadata.title = "Today";
  library.createNote(metadata, "daily body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  const auto results = index.search("Today");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "today-note");
}

MICRONOTES_TEST(library_index_refresh_preserves_search_after_note_move) {
  const micronotes::tests::TempDir rootDir("micronotes-index-move-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-move";
  metadata.title = "Movable";
  const auto path = library.createNote(metadata, "moving needle body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  const auto moved = library.moveNote(path, "archive");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "note-move");
  MICRONOTES_REQUIRE(results[0].path == moved);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_note_from_sqlite) {
  const micronotes::tests::TempDir rootDir("micronotes-index-delete-note-test");
  const auto& root = rootDir.path();

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "delete-note";
  metadata.title = "Delete Note";
  const auto path = library.createNote(metadata, "deleted searchable body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").size() == 1);
  library.deleteNote(path);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").empty());
  MICRONOTES_REQUIRE(index.size() == 0);

}

MICRONOTES_TEST(library_index_refresh_removes_trashed_folder_notes_from_sqlite) {
  const micronotes::tests::TempDir rootDir("micronotes-index-delete-folder-test");
  const auto& root = rootDir.path();

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata first;
  first.id = "delete-folder-1";
  first.title = "Folder One";
  auto firstPath = library.createNote(first, "folder-one searchable body");
  library.moveNote(firstPath, "work");
  micronotes::library::NoteMetadata second;
  second.id = "delete-folder-2";
  second.title = "Folder Two";
  auto secondPath = library.createNote(second, "folder-two searchable body");
  library.moveNote(secondPath, "work/nested");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").size() == 2);
  library.deleteFolder("work");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").empty());
  MICRONOTES_REQUIRE(index.size() == 0);

}

// --- refresh cost ------------------------------------------------------------
//
// refreshChangedFiles runs on every window focus. It used to open a connection,
// compile five statements, and take BEGIN IMMEDIATE -- forcing a write-lock and
// a WAL commit -- before it had established whether anything had changed at
// all. These tests pin the shape of the fix; a regression is invisible from the
// UI, which is exactly why it needs a test.

MICRONOTES_TEST(library_index_uses_one_connection_for_its_lifetime) {
  const micronotes::tests::TempDir rootDir("micronotes-index-conn");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "One";
  library.createNote(metadata, "body one");

  microcore::perf::resetCounters();
  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  index.refreshChangedFiles();
  index.refreshChangedFiles();
  (void)index.search("body");

  MICRONOTES_REQUIRE(
    microcore::perf::readCounter(microcore::perf::CounterId::SqliteConnectionOpens) == 1);
}

MICRONOTES_TEST(library_index_refresh_writes_nothing_when_nothing_changed) {
  const micronotes::tests::TempDir rootDir("micronotes-index-noop");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  for(int i = 0; i < 5; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n" + std::to_string(i);
    metadata.title = "Note " + std::to_string(i);
    library.createNote(metadata, "body " + std::to_string(i));
  }

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());  // cold: indexes everything
  MICRONOTES_REQUIRE(index.size() == 5);

  // A second refresh with an untouched tree must not begin a transaction, and
  // must not re-read a single note body.
  const auto execBefore = microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls);
  const auto rereadBefore = microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls) == execBefore);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == rereadBefore);
  MICRONOTES_REQUIRE(index.size() == 5);

  // Changing one note must still be picked up, and must re-read only that one.
  library.createNote([]{
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n99";
    metadata.title = "Added";
    return metadata;
  }(), "a new body");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == rereadBefore + 1);
  MICRONOTES_REQUIRE(index.size() == 6);
}

// The walk used to descend into the state directory -- the sqlite index, its
// WAL, and every attachment -- and then discard the results by comparing path
// prefixes, rebuilding the prefix string for every entry in the tree.
MICRONOTES_TEST(library_index_scan_does_not_descend_into_the_state_directory) {
  const micronotes::tests::TempDir rootDir("micronotes-index-statedir");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "One";
  library.createNote(metadata, "body");

  // Plant files inside the state directory that the walk must never visit.
  const auto attachments = root / ".micronotes" / "attachments" / "n1";
  std::filesystem::create_directories(attachments);
  for(int i = 0; i < 25; ++i) {
    std::ofstream out(attachments / ("file" + std::to_string(i) + ".md"));
    out << "not a note";
  }

  microcore::perf::resetCounters();
  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  // One real note indexed, and the 25 planted .md files never even visited.
  MICRONOTES_REQUIRE(index.size() == 1);
  const auto visited = microcore::perf::readCounter(microcore::perf::CounterId::LibraryDirectoryEntriesVisited);
  MICRONOTES_REQUIRE(visited < 25);
}

// The fts row for a note is filed under the note row's own rowid, which is what
// lets a re-index delete it without scanning the whole index. Get that wrong
// and the failure is not an error: it is an old copy of the note left behind in
// the index, so a word the writer deleted keeps returning the note for as long
// as the library exists.
MICRONOTES_TEST(library_index_replaces_a_note_rather_than_adding_a_second_copy) {
  const micronotes::tests::TempDir rootDir("micronotes-index-reindex");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "reindexed";
  metadata.title = "Reindexed";
  const auto path = library.createNote(metadata, "the original zarquon body");
  // A second note, so the index has more than one row and a rowid mix-up has
  // somewhere to go.
  micronotes::library::NoteMetadata other;
  other.id = "other";
  other.title = "Other";
  library.createNote(other, "an unrelated note");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("zarquon").size() == 1);

  // Rewrite it. The mtime has to move or the refresh is right to skip the file.
  library.saveNote(path, metadata, "the replacement blorple body");
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() +
                                           std::chrono::seconds(2));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());
  MICRONOTES_REQUIRE(index.search("unrelated").size() == 1);

  // And a removal takes its fts row with it.
  std::filesystem::remove(path);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").empty());
  MICRONOTES_REQUIRE(index.search("unrelated").size() == 1);
}

// An index built by an earlier run is reopened, not rebuilt, so whatever
// `migrate` decided about it has to leave the standing rows searchable.
MICRONOTES_TEST(library_index_reopens_an_existing_index_and_still_finds_its_notes) {
  const micronotes::tests::TempDir rootDir("micronotes-index-reopen");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "kept";
  metadata.title = "Kept";
  library.createNote(metadata, "a note with the word plugh in it");
  {
    micronotes::library::LibraryIndex index;
    MICRONOTES_REQUIRE(index.open(root));
    MICRONOTES_REQUIRE(index.refreshChangedFiles());
    MICRONOTES_REQUIRE(index.search("plugh").size() == 1);
  }
  micronotes::library::LibraryIndex reopened;
  MICRONOTES_REQUIRE(reopened.open(root));
  MICRONOTES_REQUIRE(reopened.search("plugh").size() == 1);
  // And a refresh over an unchanged tree neither loses nor duplicates it.
  MICRONOTES_REQUIRE(reopened.refreshChangedFiles());
  MICRONOTES_REQUIRE(reopened.search("plugh").size() == 1);
}

// The refresh a save takes: one named file, no walk of the tree and no read of
// every row in the table. `refreshChangedFiles` exists to *discover* what
// changed and pays for the discovery; a save already knows.
MICRONOTES_TEST(library_index_refreshes_one_named_file_without_walking_the_tree) {
  const micronotes::tests::TempDir rootDir("micronotes-index-one-file");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  std::vector<std::filesystem::path> paths;
  for(int i = 0; i < 20; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n" + std::to_string(i);
    metadata.title = "Note " + std::to_string(i);
    paths.push_back(library.createNote(metadata, "body " + std::to_string(i)));
  }

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.size() == 20);

  microcore::perf::resetCounters();
  micronotes::library::NoteMetadata changed;
  changed.id = "n7";
  changed.title = "Note 7";
  MICRONOTES_REQUIRE(library.saveNote(paths[7], changed, "rewritten needle\n"));
  const auto refresh = index.refreshFile(paths[7]);
  MICRONOTES_REQUIRE(refresh.ok);
  // The five fields the note list is built from did not move, so the sidebar,
  // the folder counts and the tag list did not have to be rebuilt.
  MICRONOTES_REQUIRE(!refresh.listFieldsChanged);
  // One file read, and the tree never walked.
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 1);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryNoteFilesCalls) == 0);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexRefreshCalls) == 0);
  // And the body reached the search index.
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);
  MICRONOTES_REQUIRE(index.size() == 20);

  // A title change is a note-list change, and says so.
  changed.title = "Renamed 7";
  MICRONOTES_REQUIRE(library.saveNote(paths[7], changed, "rewritten needle\n"));
  MICRONOTES_REQUIRE(index.refreshFile(paths[7]).listFieldsChanged);

  // An unchanged file is a stat and nothing else: no read, no transaction.
  microcore::perf::resetCounters();
  const auto execBefore = microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls);
  MICRONOTES_REQUIRE(index.refreshFile(paths[7]).ok);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 0);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls) == execBefore);

  // A file that is gone takes its rows with it, and that is a list change.
  std::filesystem::remove(paths[3]);
  const auto removed = index.refreshFile(paths[3]);
  MICRONOTES_REQUIRE(removed.ok);
  MICRONOTES_REQUIRE(removed.listFieldsChanged);
  MICRONOTES_REQUIRE(index.size() == 19);
  MICRONOTES_REQUIRE(index.search("body 3").empty());

}

// The form a save takes: the caller has just written the file, so it hands over
// the front matter and body instead of making the index read them back.
MICRONOTES_TEST(library_index_indexes_a_written_file_without_reading_it_back) {
  const micronotes::tests::TempDir rootDir("micronotes-index-written");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "Written";
  metadata.tags = {"alpha"};
  const auto path = library.createNote(metadata, "first body\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  microcore::perf::resetCounters();
  MICRONOTES_REQUIRE(library.saveNote(path, metadata, "second body with a needle\n"));
  const auto refresh = index.refreshWrittenFile(path, metadata, "second body with a needle\n");
  MICRONOTES_REQUIRE(refresh.ok);
  MICRONOTES_REQUIRE(!refresh.listFieldsChanged);
  // Nothing was read off the disk.
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 0);
  // And the rows say the same thing they would have if it had been.
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);
  MICRONOTES_REQUIRE(index.search("first body").empty());
  const auto notes = index.notes();
  MICRONOTES_REQUIRE(notes.size() == 1);
  MICRONOTES_REQUIRE(notes.front().title == "Written");
  MICRONOTES_REQUIRE(notes.front().tags.size() == 1);
  MICRONOTES_REQUIRE(notes.front().tags.front() == "alpha");

  // The handed-over form and the read form have to agree, or a save and a
  // watcher wake-up would index the same note differently.
  MICRONOTES_REQUIRE(!index.refreshFile(path).listFieldsChanged);
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);

}

// The index used to carry a second copy of every note: `notes.body` and
// `notes_fts.body` both held the whole text, so a 10.3 MB library indexed to
// 27.2 MB. The full-text table stores the terms and not the text now -- the
// only thing ever asked of it is which rowids match, and the snippets are built
// from `notes.body` through the join.
//
// The shape depends on the SQLite the build is linked against, because a
// contentless fts5 table can only have a row deleted from 3.43 onwards and
// deleting a row is what every save does. Both shapes have to answer every
// question identically, which is what makes the older one a fallback rather
// than a second mode -- so the assertions below are about *search*, and the
// shape is only read to say which one they were checked against.
MICRONOTES_TEST(library_index_stores_the_terms_rather_than_the_text) {
  const micronotes::tests::TempDir rootDir("micronotes-index-contentless");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "terms";
  metadata.title = "Terms";
  const auto path = library.createNote(metadata, "a note about zarquon and nothing else");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.ftsStoresBodies() == (sqlite3_libversion_number() < 3043000));

  // Matching, in all three scopes.
  MICRONOTES_REQUIRE(index.search("zarquon").size() == 1);
  MICRONOTES_REQUIRE(index.search("Terms", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon", micronotes::library::SearchScope::Content).size() == 1);
  // And the snippet, which is the thing that would go missing if it had been
  // coming out of the full-text table rather than out of `notes`.
  const auto zarquon = index.search("zarquon");
  MICRONOTES_REQUIRE(zarquon.front().firstMatch() != nullptr);
  MICRONOTES_REQUIRE(zarquon.front().firstMatch()->matchLine.find("zarquon") !=
                     std::string::npos);

  // A rewrite has to retire the old terms. This is the operation a contentless
  // table cannot perform before 3.43, and getting it wrong leaves a note
  // matching words it no longer contains.
  library.saveNote(path, metadata, "a note about blorple and nothing else");
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() +
                                           std::chrono::seconds(2));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());

  // And a full rebuild, which empties the table rather than deleting rows.
  MICRONOTES_REQUIRE(index.rebuild());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());
}
