#include "TestSupport.h"

#include "library/Library.h"
#include "library/Metadata.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

static std::size_t trashFileCount(const std::filesystem::path& xdgDataHome) {
  const auto trashFiles = xdgDataHome / "Trash" / "files";
  if(!std::filesystem::exists(trashFiles)) return 0;
  return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(trashFiles), std::filesystem::directory_iterator()));
}

}

MICRONOTES_TEST(metadata_header_contains_stable_id) {
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-1";
  metadata.title = "Fast note";
  metadata.tags = {"fast", "local"};
  const auto header = micronotes::library::metadataHeader(metadata);
  MICRONOTES_REQUIRE(header.find("id: note-1") != std::string::npos);
  MICRONOTES_REQUIRE(header.find("tags: fast local") != std::string::npos);
}

MICRONOTES_TEST(library_creates_reads_and_renames_note_without_losing_id) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-library-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "stable-id";
  metadata.title = "Original";
  const auto path = library.createNote(metadata, "body");
  MICRONOTES_REQUIRE(std::filesystem::exists(path));
  MICRONOTES_REQUIRE(library.loadNoteBody(path) == "body");
  const auto renamed = library.renameNote(path, "Renamed");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(renamed).id == "stable-id");
  MICRONOTES_REQUIRE(!std::filesystem::exists(path));
  const auto moved = library.moveNote(renamed, "folder");
  MICRONOTES_REQUIRE(std::filesystem::exists(moved));
  MICRONOTES_REQUIRE(library.loadNoteMetadata(moved).id == "stable-id");
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_loads_metadata_and_body_in_one_read) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-library-load-note-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "combined";
  metadata.title = "Combined Read";
  metadata.tags = {"fast", "single-read"};
  const auto path = library.createNote(metadata, "# Heading\n\nbody");

  const auto note = library.loadNote(path);
  MICRONOTES_REQUIRE(note.metadata.id == "combined");
  MICRONOTES_REQUIRE(note.metadata.title == "Combined Read");
  MICRONOTES_REQUIRE(note.metadata.tags.size() == 2);
  MICRONOTES_REQUIRE(note.body == "# Heading\n\nbody");
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_reads_metadata_from_header_without_body_scan) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-library-header-only-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "header-only";
  metadata.title = "Header Only";
  const std::string body = std::string(1024 * 1024, 'x') + "\n---\nid: body-frontmatter-looking\n---\n";
  const auto path = library.createNote(metadata, body);

  const auto loadedMetadata = library.loadNoteMetadata(path);
  MICRONOTES_REQUIRE(loadedMetadata.id == "header-only");
  MICRONOTES_REQUIRE(loadedMetadata.title == "Header Only");
  MICRONOTES_REQUIRE(library.loadNoteBody(path) == body);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_never_overwrites_notes_on_path_collisions) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-collision-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);

  micronotes::library::NoteMetadata first;
  first.id = "first";
  first.title = "Untitled";
  const auto firstPath = library.createNote(first, "first body");

  micronotes::library::NoteMetadata second;
  second.id = "second";
  second.title = "Untitled";
  const auto secondPath = library.createNote(second, "second body");
  MICRONOTES_REQUIRE(firstPath != secondPath);
  MICRONOTES_REQUIRE(firstPath.filename() == "Untitled.md");
  MICRONOTES_REQUIRE(secondPath.filename() == "Untitled-2.md");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(firstPath).id == "first");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(secondPath).id == "second");

  micronotes::library::NoteMetadata renamedSource;
  renamedSource.id = "rename-source";
  renamedSource.title = "Rename Source";
  const auto renameSourcePath = library.createNote(renamedSource, "rename body");
  const auto renamedPath = library.renameNote(renameSourcePath, "Untitled");
  MICRONOTES_REQUIRE(renamedPath.filename() == "Untitled-3.md");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(firstPath).id == "first");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(renamedPath).id == "rename-source");

  const auto movedFirst = library.moveNote(firstPath, "work");
  micronotes::library::NoteMetadata moveSource;
  moveSource.id = "move-source";
  moveSource.title = "Untitled";
  const auto moveSourcePath = library.createNote(moveSource, "move body");
  const auto movedSecond = library.moveNote(moveSourcePath, "work");
  MICRONOTES_REQUIRE(movedFirst.filename() == "Untitled.md");
  MICRONOTES_REQUIRE(movedSecond.filename() == "Untitled-2.md");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(movedFirst).id == "first");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(movedSecond).id == "move-source");

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_rejects_paths_outside_root) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-boundary-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  micronotes::library::Library library(root);
  bool rejected = false;
  try {
    (void)library.loadNoteBody(root / ".." / "escape.md");
  } catch(...) {
    rejected = true;
  }
  MICRONOTES_REQUIRE(rejected);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_persists_tag_updates) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-tags-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "tag-note";
  metadata.title = "Tagged";
  const auto path = library.createNote(metadata, "body");
  library.updateTags(path, {"fast", "local"});
  MICRONOTES_REQUIRE(library.loadNoteMetadata(path).tags.size() == 2);
  library.updateTags(path, {"local"});
  const auto updated = library.loadNoteMetadata(path);
  MICRONOTES_REQUIRE(updated.tags.size() == 1);
  MICRONOTES_REQUIRE(updated.tags[0] == "local");
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_delete_note_moves_note_and_attachments_to_trash) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-trash-note-test";
  const auto xdg = std::filesystem::temp_directory_path() / "micronotes-trash-note-xdg";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
  ScopedXdgDataHome scopedTrash(xdg);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-trash";
  metadata.title = "Trash Me";
  const auto path = library.createNote(metadata, "body");
  const auto attachmentDir = root / ".micronotes" / "attachments" / metadata.id;
  std::filesystem::create_directories(attachmentDir);
  {
    std::ofstream out(attachmentDir / "image.png");
    out << "png";
  }

  library.deleteNote(path);
  MICRONOTES_REQUIRE(!std::filesystem::exists(path));
  MICRONOTES_REQUIRE(!std::filesystem::exists(attachmentDir));
  MICRONOTES_REQUIRE(trashFileCount(xdg) == 2);
  MICRONOTES_REQUIRE(std::filesystem::exists(xdg / "Trash" / "info" / "Trash-Me.md.trashinfo"));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
}

MICRONOTES_TEST(library_delete_folder_moves_folder_notes_and_attachments_to_trash) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-trash-folder-test";
  const auto xdg = std::filesystem::temp_directory_path() / "micronotes-trash-folder-xdg";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
  ScopedXdgDataHome scopedTrash(xdg);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata first;
  first.id = "folder-note-1";
  first.title = "First";
  auto firstPath = library.createNote(first, "one");
  firstPath = library.moveNote(firstPath, "work");
  micronotes::library::NoteMetadata second;
  second.id = "folder-note-2";
  second.title = "Second";
  auto secondPath = library.createNote(second, "two");
  secondPath = library.moveNote(secondPath, "work/nested");
  const auto attachmentOne = root / ".micronotes" / "attachments" / first.id;
  const auto attachmentTwo = root / ".micronotes" / "attachments" / second.id;
  std::filesystem::create_directories(attachmentOne);
  std::filesystem::create_directories(attachmentTwo);

  library.deleteFolder("work");
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "work"));
  MICRONOTES_REQUIRE(!std::filesystem::exists(attachmentOne));
  MICRONOTES_REQUIRE(!std::filesystem::exists(attachmentTwo));
  MICRONOTES_REQUIRE(trashFileCount(xdg) == 3);
  MICRONOTES_REQUIRE(std::filesystem::exists(xdg / "Trash" / "info" / "work.trashinfo"));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(xdg);
}
