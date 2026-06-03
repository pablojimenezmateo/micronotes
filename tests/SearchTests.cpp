#include "TestSupport.h"

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"
#include "library/Organization.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

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

MICRONOTES_TEST(library_index_searches_cached_rows) {
  micronotes::library::LibraryIndex index;
  index.add({"1", "work/today.md", "Today"});
  index.add({"2", "personal/list.md", "List"});
  const auto results = index.search("Today");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "1");
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
  const auto xdg = std::filesystem::temp_directory_path() / "micronotes-index-delete-note-xdg";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
  ScopedXdgDataHome scopedTrash(xdg);

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
  std::filesystem::remove_all(xdg);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_folder_notes_from_sqlite) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-delete-folder-test";
  const auto xdg = std::filesystem::temp_directory_path() / "micronotes-index-delete-folder-xdg";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
  ScopedXdgDataHome scopedTrash(xdg);

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
  std::filesystem::remove_all(xdg);
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
