#include "TestSupport.h"

#include "library/Library.h"
#include "library/Metadata.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

static std::size_t trashFileCount(const std::filesystem::path& root) {
  const auto files = root / ".micronotes" / "trash" / "files";
  if(!std::filesystem::exists(files)) return 0;
  return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(files), std::filesystem::directory_iterator()));
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
  std::filesystem::remove_all(root);

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
  // The note and its attachments, filed inside the library rather than in the
  // desktop's trash, so restoring is something micronotes can actually do.
  MICRONOTES_REQUIRE(trashFileCount(root) == 2);
  // And gone from the library the moment it moved: `.micronotes` is not scanned.
  MICRONOTES_REQUIRE(library.noteFiles().empty());

  const auto entries = library.trashEntries();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().title == "Trash Me");
  MICRONOTES_REQUIRE(!entries.front().deletedAt.empty());

  MICRONOTES_REQUIRE(library.restoreFromTrash(entries.front().name));
  MICRONOTES_REQUIRE(std::filesystem::exists(path));
  MICRONOTES_REQUIRE(std::filesystem::exists(attachmentDir / "image.png"));
  MICRONOTES_REQUIRE(library.loadNoteMetadata(path).id == "note-trash");
  // Restoring takes the entry off the list; it is not an offer twice.
  MICRONOTES_REQUIRE(library.trashEntries().empty());
  MICRONOTES_REQUIRE(trashFileCount(root) == 0);

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_restore_does_not_overwrite_what_took_the_name_back) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-trash-collide-test";
  std::filesystem::remove_all(root);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "first";
  metadata.title = "Notes";
  const auto path = library.createNote(metadata, "original");
  library.deleteNote(path);

  micronotes::library::NoteMetadata replacement;
  replacement.id = "second";
  replacement.title = "Notes";
  const auto replacementPath = library.createNote(replacement, "replacement");
  MICRONOTES_REQUIRE(replacementPath == path);

  MICRONOTES_REQUIRE(library.restoreFromTrash(library.trashEntries().front().name));
  MICRONOTES_REQUIRE(library.loadNoteMetadata(path).id == "second");
  MICRONOTES_REQUIRE(library.noteFiles().size() == 2);

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_delete_folder_moves_folder_notes_and_attachments_to_trash) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-trash-folder-test";
  std::filesystem::remove_all(root);

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
  MICRONOTES_REQUIRE(trashFileCount(root) == 3);

  // The folder is one offer, not three: the attachment directories that went
  // with it are filed so they can be restored, not so they can be chosen.
  const auto entries = library.trashEntries();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().title == "work");

  MICRONOTES_REQUIRE(library.restoreFromTrash(entries.front().name));
  MICRONOTES_REQUIRE(std::filesystem::exists(firstPath));
  MICRONOTES_REQUIRE(std::filesystem::exists(secondPath));
  MICRONOTES_REQUIRE(std::filesystem::exists(attachmentOne));
  MICRONOTES_REQUIRE(std::filesystem::exists(attachmentTwo));
  MICRONOTES_REQUIRE(library.trashEntries().empty());

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(metadata_keeps_front_matter_it_does_not_understand) {
  // The bug this guards: micronotes rewrites the whole header on save, so a key
  // it cannot parse used to be destroyed by the first autosave after opening a
  // note written elsewhere.
  const std::string source =
      "---\n"
      "id: outside\n"
      "title: From Another Tool\n"
      "icon: \xF0\x9F\x93\x93\n"
      "tags: alpha beta\n"
      "aliases:\n"
      "  - first\n"
      "  - second\n"
      "cssclass: wide\n"
      "---\n\n"
      "body\n";
  const auto metadata = micronotes::library::parseMetadata(source);
  MICRONOTES_REQUIRE(metadata.id == "outside");
  MICRONOTES_REQUIRE(metadata.icon == "\xF0\x9F\x93\x93");
  MICRONOTES_REQUIRE(metadata.tags.size() == 2);
  MICRONOTES_REQUIRE(metadata.extra.size() == 4);
  MICRONOTES_REQUIRE(metadata.extra.front() == "aliases:");
  MICRONOTES_REQUIRE(metadata.extra.back() == "cssclass: wide");

  const auto rewritten = micronotes::library::metadataHeader(metadata);
  MICRONOTES_REQUIRE(rewritten.find("aliases:\n  - first\n  - second\ncssclass: wide\n") != std::string::npos);
  MICRONOTES_REQUIRE(rewritten.find("icon: \xF0\x9F\x93\x93\n") != std::string::npos);
  // And a second pass through changes nothing more.
  MICRONOTES_REQUIRE(micronotes::library::metadataHeader(
                         micronotes::library::parseMetadata(rewritten + "body\n")) == rewritten);
}

MICRONOTES_TEST(metadata_does_not_invent_a_tags_key) {
  // A note that arrived with no tags must come back with no tags. The header is
  // rewritten in full on every save, so an unconditional `tags:` line meant the
  // first autosave after opening someone else's note changed it.
  const auto note = micronotes::library::parseMetadata("---\nid: n\ntitle: Plain\n---\n\nbody\n");
  MICRONOTES_REQUIRE(note.tags.empty());
  MICRONOTES_REQUIRE(micronotes::library::metadataHeader(note) == "---\nid: n\ntitle: Plain\n---\n\n");
}

MICRONOTES_TEST(metadata_writes_tags_back_in_the_form_it_read_them) {
  const auto block = micronotes::library::parseMetadata(
      "---\ntitle: Block\ntags:\n  - one\n  - two\n---\n\nbody\n");
  MICRONOTES_REQUIRE(block.tags.size() == 2 && block.tags[1] == "two");
  MICRONOTES_REQUIRE(block.extra.empty());
  MICRONOTES_REQUIRE(micronotes::library::metadataHeader(block).find("tags:\n  - one\n  - two\n") != std::string::npos);

  const auto flow = micronotes::library::parseMetadata("---\ntitle: Flow\ntags: [one, two]\n---\n\nbody\n");
  MICRONOTES_REQUIRE(flow.tags.size() == 2 && flow.tags[0] == "one");
  MICRONOTES_REQUIRE(micronotes::library::metadataHeader(flow).find("tags: [one, two]\n") != std::string::npos);

  // No icon means no `icon:` line at all, so a note micronotes never gave one
  // reads exactly as it did before.
  MICRONOTES_REQUIRE(micronotes::library::metadataHeader(flow).find("icon:") == std::string::npos);
}

MICRONOTES_TEST(library_restores_a_folder_without_giving_it_a_file_extension) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-trash-folder-collide";
  std::filesystem::remove_all(root);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "in-folder";
  metadata.title = "Inside";
  library.moveNote(library.createNote(metadata, "one"), "work");
  library.deleteFolder("work");
  // A new folder takes the name back before the old one is restored.
  library.createFolder("work");

  MICRONOTES_REQUIRE(library.restoreFromTrash(library.trashEntries().front().name));
  MICRONOTES_REQUIRE(std::filesystem::is_directory(root / "work-2"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work-2" / "Inside.md"));

  std::filesystem::remove_all(root);
}
