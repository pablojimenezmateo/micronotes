#include "TestSupport.h"

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"
#include "library/Organization.h"
#include "core/perf/PerformanceCounters.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>

namespace {

// Points the index at a scratch XDG_DATA_HOME so a test run never touches the
// real one, and restores whatever was there on the way out.
class ScopedXdgDataHome {
public:
  explicit ScopedXdgDataHome(const std::filesystem::path& path) {
    if(const char* current = std::getenv("XDG_DATA_HOME")) {
      previous_ = current;
      hadPrevious_ = true;
    }
    setenv("XDG_DATA_HOME", path.c_str(), 1);
  }

  ~ScopedXdgDataHome() {
    if(hadPrevious_) setenv("XDG_DATA_HOME", previous_.c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
  }

private:
  std::string previous_;
  bool hadPrevious_ = false;
};

}


MICRONOTES_TEST(library_index_searches_file_backed_rows) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-file-backed-test";
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_rebuilds_sqlite_cache_from_files) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-search";
  metadata.title = "SQLite Fast Path";
  library.createNote(metadata, "needle body\nmiddle\nsecond needle line");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "note-search");
  MICRONOTES_REQUIRE(results[0].matchLine == "needle body");
  MICRONOTES_REQUIRE(results[0].snippets.size() == 2);
  const auto partial = index.search("eedle bo");
  MICRONOTES_REQUIRE(partial.size() == 1);
  MICRONOTES_REQUIRE(partial[0].id == "note-search");
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Content).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Title).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Content).size() == 1);
  MICRONOTES_REQUIRE(std::filesystem::exists(root / ".micronotes" / "index.sqlite"));
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_preserves_search_after_note_move) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-move-test";
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_note_from_sqlite) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-delete-note-test";
  std::filesystem::remove_all(root);

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

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_folder_notes_from_sqlite) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-delete-folder-test";
  std::filesystem::remove_all(root);

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

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(organization_lists_folders_tags_and_notes) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-org-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata work;
  work.id = "work-note";
  work.title = "Alpha";
  work.tags = {"work", "fast"};
  const auto path = library.createNote(work, "body");
  library.moveNote(path, "work");

  micronotes::library::NoteMetadata personal;
  personal.id = "personal-note";
  personal.title = "Beta";
  personal.tags = {"home"};
  library.createNote(personal, "body");
  library.createFolder("ideas");

  micronotes::library::OrganizationService org(library);
  MICRONOTES_REQUIRE(org.folders().size() == 3);
  MICRONOTES_REQUIRE(org.tags().size() == 3);
  MICRONOTES_REQUIRE(org.notesInFolder("ideas").empty());
  MICRONOTES_REQUIRE(org.notesInFolder("work").size() == 1);
  MICRONOTES_REQUIRE(org.notesWithTag("fast").size() == 1);
  std::filesystem::remove_all(root);
}

// --- refresh cost ------------------------------------------------------------
//
// refreshChangedFiles runs on every window focus. It used to open a connection,
// compile five statements, and take BEGIN IMMEDIATE -- forcing a write-lock and
// a WAL commit -- before it had established whether anything had changed at
// all. These tests pin the shape of the fix; a regression is invisible from the
// UI, which is exactly why it needs a test.

MICRONOTES_TEST(library_index_uses_one_connection_for_its_lifetime) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-conn";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

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
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_writes_nothing_when_nothing_changed) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-noop";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

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
  std::filesystem::remove_all(root);
}

// The walk used to descend into the state directory -- the sqlite index, its
// WAL, and every attachment -- and then discard the results by comparing path
// prefixes, rebuilding the prefix string for every entry in the tree.
MICRONOTES_TEST(library_index_scan_does_not_descend_into_the_state_directory) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-statedir";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

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
  std::filesystem::remove_all(root);
}
