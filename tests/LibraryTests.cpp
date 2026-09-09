#include "TestSupport.h"
#include "TempDir.h"

#include "library/Library.h"
#include "library/Metadata.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
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
  const micronotes::tests::TempDir rootDir("micronotes-library-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "stable-id";
  metadata.title = "Original";
  const auto path = library.createNote(metadata, "body");
  MICRONOTES_REQUIRE(std::filesystem::exists(path));
  MICRONOTES_REQUIRE(library.loadNote(path).body == "body");
  const auto renamed = library.renameNote(path, "Renamed");
  MICRONOTES_REQUIRE(library.loadNoteMetadata(renamed).id == "stable-id");
  MICRONOTES_REQUIRE(!std::filesystem::exists(path));
  const auto moved = library.moveNote(renamed, "folder");
  MICRONOTES_REQUIRE(std::filesystem::exists(moved));
  MICRONOTES_REQUIRE(library.loadNoteMetadata(moved).id == "stable-id");
}

MICRONOTES_TEST(library_loads_metadata_and_body_in_one_read) {
  const micronotes::tests::TempDir rootDir("micronotes-library-load-note-test");
  const auto& root = rootDir.path();
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
}

MICRONOTES_TEST(library_reads_metadata_from_header_without_body_scan) {
  const micronotes::tests::TempDir rootDir("micronotes-library-header-only-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "header-only";
  metadata.title = "Header Only";
  const std::string body = std::string(1024 * 1024, 'x') + "\n---\nid: body-frontmatter-looking\n---\n";
  const auto path = library.createNote(metadata, body);

  const auto loadedMetadata = library.loadNoteMetadata(path);
  MICRONOTES_REQUIRE(loadedMetadata.id == "header-only");
  MICRONOTES_REQUIRE(loadedMetadata.title == "Header Only");
  MICRONOTES_REQUIRE(library.loadNote(path).body == body);
}

MICRONOTES_TEST(library_never_overwrites_notes_on_path_collisions) {
  const micronotes::tests::TempDir rootDir("micronotes-collision-test");
  const auto& root = rootDir.path();
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

}

MICRONOTES_TEST(library_rejects_paths_outside_root) {
  const micronotes::tests::TempDir rootDir("micronotes-boundary-test");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  micronotes::library::Library library(root);
  bool rejected = false;
  try {
    (void)library.loadNote(root / ".." / "escape.md");
  } catch(...) {
    rejected = true;
  }
  MICRONOTES_REQUIRE(rejected);
}

// Tags round-trip through the front matter, and through the one write that
// changes a header without touching the body. This used to go through
// `Library::updateTags`, which nothing in the app called: `AppState` writes the
// header itself, so the test was the only user of a second way to do it.
MICRONOTES_TEST(library_persists_tag_updates) {
  const micronotes::tests::TempDir rootDir("micronotes-tags-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "tag-note";
  metadata.title = "Tagged";
  const auto path = library.createNote(metadata, "body");

  metadata.tags = {"fast", "local"};
  MICRONOTES_REQUIRE(library.saveNote(path, metadata, library.loadNote(path).body));
  MICRONOTES_REQUIRE(library.loadNoteMetadata(path).tags.size() == 2);
  metadata.tags = {"local"};
  MICRONOTES_REQUIRE(library.saveNote(path, metadata, library.loadNote(path).body));
  const auto updated = library.loadNoteMetadata(path);
  MICRONOTES_REQUIRE(updated.tags.size() == 1);
  MICRONOTES_REQUIRE(updated.tags[0] == "local");
  MICRONOTES_REQUIRE(library.loadNote(path).body == "body");
}

MICRONOTES_TEST(library_delete_note_moves_note_and_attachments_to_trash) {
  const micronotes::tests::TempDir rootDir("micronotes-trash-note-test");
  const auto& root = rootDir.path();

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

}

MICRONOTES_TEST(library_restore_does_not_overwrite_what_took_the_name_back) {
  const micronotes::tests::TempDir rootDir("micronotes-trash-collide-test");
  const auto& root = rootDir.path();

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

}

MICRONOTES_TEST(library_delete_folder_moves_folder_notes_and_attachments_to_trash) {
  const micronotes::tests::TempDir rootDir("micronotes-trash-folder-test");
  const auto& root = rootDir.path();

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
  const micronotes::tests::TempDir rootDir("micronotes-trash-folder-collide");
  const auto& root = rootDir.path();

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

}

// --- the note's own name, in the body -----------------------------------
//
// The page draws a note's name above its first block, so a note that also
// carries the name as its first heading printed it twice. The heading is split
// off with the front matter and written back with it: the body the user edits
// holds the name once, and the file on disk still holds the heading every other
// tool expects there.

namespace {

// Writes `markdown` verbatim, bypassing createNote, because what is being
// tested is what happens to a file micronotes did not write.
std::filesystem::path writeNote(const std::filesystem::path& root, const std::string& name,
                                const std::string& markdown) {
  std::filesystem::create_directories(root);
  std::ofstream out(root / name, std::ios::binary);
  out << markdown;
  out.close();
  return root / name;
}

std::string readNote(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

MICRONOTES_TEST(library_reads_a_leading_title_heading_as_the_note_header) {
  const micronotes::tests::TempDir rootDir("micronotes-title-heading");
  const auto& root = rootDir.path();
  const micronotes::library::Library library(root);

  const auto path = writeNote(root, "Welcome.md",
    "---\nid: welcome\ntitle: Welcome\n---\n\n# Welcome\n\nThe first paragraph.\n");
  const auto note = library.loadNote(path);
  MICRONOTES_REQUIRE(note.metadata.titleHeading);
  MICRONOTES_REQUIRE(note.body == "The first paragraph.\n");

  // And back onto the file exactly as it was found: the heading is the note's
  // header, not something micronotes is free to drop.
  MICRONOTES_REQUIRE(library.saveNote(path, note.metadata, note.body));
  MICRONOTES_REQUIRE(readNote(path) ==
    "---\nid: welcome\ntitle: Welcome\n---\n\n# Welcome\n\nThe first paragraph.\n");
}

// No front matter at all: the name is the file's stem, and a heading repeating
// it is still the note's own name.
MICRONOTES_TEST(library_reads_a_title_heading_against_the_file_stem) {
  const micronotes::tests::TempDir rootDir("micronotes-title-heading-stem");
  const auto& root = rootDir.path();
  const micronotes::library::Library library(root);

  const auto path = writeNote(root, "Shopping List.md", "# Shopping List\n\nMilk.\n");
  const auto note = library.loadNote(path);
  MICRONOTES_REQUIRE(note.metadata.titleHeading);
  MICRONOTES_REQUIRE(note.body == "Milk.\n");
}

// A heading that says something the name does not is body text, and stays put.
// This is the case that must not be swallowed: getting it wrong hides a line
// the reader wrote.
MICRONOTES_TEST(library_leaves_a_heading_that_is_not_the_notes_name_alone) {
  const micronotes::tests::TempDir rootDir("micronotes-title-heading-other");
  const auto& root = rootDir.path();
  const micronotes::library::Library library(root);

  const auto differs = library.loadNote(writeNote(root, "Notes.md",
    "---\nid: n\ntitle: Notes\n---\n\n# Something Else\n\nBody.\n"));
  MICRONOTES_REQUIRE(!differs.metadata.titleHeading);
  MICRONOTES_REQUIRE(differs.body == "# Something Else\n\nBody.\n");

  // A deeper heading is a section of the note, however it is spelt.
  const auto deeper = library.loadNote(writeNote(root, "Deep.md",
    "---\nid: d\ntitle: Deep\n---\n\n## Deep\n\nBody.\n"));
  MICRONOTES_REQUIRE(!deeper.metadata.titleHeading);
  MICRONOTES_REQUIRE(deeper.body == "## Deep\n\nBody.\n");

  // Case is a difference, because re-emitting the name would change the file.
  const auto cased = library.loadNote(writeNote(root, "Cased.md",
    "---\nid: c\ntitle: Cased\n---\n\n# cased\n\nBody.\n"));
  MICRONOTES_REQUIRE(!cased.metadata.titleHeading);
  MICRONOTES_REQUIRE(cased.body == "# cased\n\nBody.\n");
}

// Renaming a note that carries its name as a heading moves the heading with it.
// Leaving it behind is what used to happen, and left the file naming the note
// by a title it no longer had.
MICRONOTES_TEST(library_rename_carries_the_title_heading_with_it) {
  const micronotes::tests::TempDir rootDir("micronotes-title-heading-rename");
  const auto& root = rootDir.path();
  const micronotes::library::Library library(root);

  const auto path = writeNote(root, "Before.md",
    "---\nid: r\ntitle: Before\n---\n\n# Before\n\nBody.\n");
  const auto renamed = library.renameNote(path, "After");
  const auto note = library.loadNote(renamed);
  MICRONOTES_REQUIRE(note.metadata.title == "After");
  MICRONOTES_REQUIRE(note.metadata.titleHeading);
  MICRONOTES_REQUIRE(note.body == "Body.\n");
  MICRONOTES_REQUIRE(readNote(renamed).find("# After\n") != std::string::npos);
}

// A note micronotes creates has no such heading, and must not grow one: the
// name is drawn from the library, and writing it into the Markdown as well is
// the duplication this whole split exists to undo.
MICRONOTES_TEST(library_creates_a_note_without_a_title_heading) {
  const micronotes::tests::TempDir rootDir("micronotes-title-heading-new");
  const auto& root = rootDir.path();
  const micronotes::library::Library library(root);

  micronotes::library::NoteMetadata metadata;
  metadata.id = "fresh";
  metadata.title = "Fresh";
  const auto path = library.createNote(metadata, "");
  MICRONOTES_REQUIRE(readNote(path).find("# Fresh") == std::string::npos);
  MICRONOTES_REQUIRE(library.loadNote(path).body.empty());
}

MICRONOTES_TEST(metadata_title_heading_length_measures_only_what_it_claims) {
  using micronotes::library::titleHeadingLength;
  // The heading line and the blank line under it.
  MICRONOTES_REQUIRE(titleHeadingLength("# A\n\nB\n", "A") == 5);
  // One blank line, not a run of them: the rest is spacing the reader asked for.
  MICRONOTES_REQUIRE(titleHeadingLength("# A\n\n\nB\n", "A") == 5);
  // No blank line at all is still a heading.
  MICRONOTES_REQUIRE(titleHeadingLength("# A\nB\n", "A") == 4);
  // A heading and nothing else.
  MICRONOTES_REQUIRE(titleHeadingLength("# A", "A") == 3);
  // Spaces around the name do not make it a different name.
  MICRONOTES_REQUIRE(titleHeadingLength("#   A  \n\nB", "A") == 9);
  // Everything it declines.
  MICRONOTES_REQUIRE(titleHeadingLength("# A\n", "") == 0);
  MICRONOTES_REQUIRE(titleHeadingLength("", "A") == 0);
  MICRONOTES_REQUIRE(titleHeadingLength("#A\n", "A") == 0);
  MICRONOTES_REQUIRE(titleHeadingLength("# A #\n", "A") == 0);
  MICRONOTES_REQUIRE(titleHeadingLength("A\n=\n", "A") == 0);
  MICRONOTES_REQUIRE(titleHeadingLength("Text\n\n# A\n", "A") == 0);
}

// The trash index is the only record of where a deleted note came from, so it
// has to reach the disk before the file moves.
//
// The old order was the other way round and the append was an unflushed
// `ofstream`: a crash in between left the note sitting in `trash/files` with
// nothing naming it, so `trashEntries()` could not list it and the person who
// deleted it had no way back to it from inside the app.
MICRONOTES_TEST(library_writes_the_trash_index_before_it_moves_anything) {
  const micronotes::tests::TempDir rootDir("micronotes-trash-order");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "ordered";
  metadata.title = "Ordered";
  const auto path = library.createNote(metadata, "body");
  library.deleteNote(path);

  const auto index = root / ".micronotes" / "trash" / "index";
  MICRONOTES_REQUIRE(std::filesystem::exists(index));
  const auto entries = library.trashEntries();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().title == "Ordered");

  // The half-completed state a crash between the two steps would leave: the
  // index line is there and the file is not. It reads as history rather than as
  // an offer that fails when taken.
  std::filesystem::remove(root / ".micronotes" / "trash" / "files" / entries.front().name);
  MICRONOTES_REQUIRE(library.trashEntries().empty());
  MICRONOTES_REQUIRE(!library.restoreFromTrash(entries.front().name));

}

// A folder delete files the folder and one entry per attachment directory under
// it. They are one durable write now, and they must not collide: nothing has
// moved yet when the names are handed out, so the filesystem check alone cannot
// tell that a name is already spoken for.
MICRONOTES_TEST(library_reserves_distinct_trash_names_within_one_folder_delete) {
  const micronotes::tests::TempDir rootDir("micronotes-trash-batch");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  library.createFolder("work");

  // Three notes in the folder, each with an attachment directory. The
  // directories are named by note id, so they are already distinct -- what has
  // to hold is that every reserved name is distinct and every file arrives.
  for(int i = 0; i < 3; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "batch-" + std::to_string(i);
    metadata.title = "Batch " + std::to_string(i);
    const auto path = library.createNote(metadata, "body");
    library.moveNote(path, "work");
    const auto attachments = root / ".micronotes" / "attachments" / metadata.id;
    std::filesystem::create_directories(attachments);
    std::ofstream(attachments / "file.png") << "png";
  }

  library.deleteFolder("work");
  // The folder plus three attachment directories, each under its own name.
  MICRONOTES_REQUIRE(trashFileCount(root) == 4);
  const auto entries = library.trashEntries();
  // Only the folder is offered; the attachment directories are filed so the
  // restore can find them, not so a person can pick one.
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().title == "work");

  MICRONOTES_REQUIRE(library.restoreFromTrash(entries.front().name));
  MICRONOTES_REQUIRE(library.noteFiles().size() == 3);
  for(int i = 0; i < 3; ++i) {
    const auto id = "batch-" + std::to_string(i);
    MICRONOTES_REQUIRE(std::filesystem::exists(root / ".micronotes" / "attachments" / id / "file.png"));
  }

}

// A directory named `files` inside a notebook holds companion files, and the
// walk reports everything under it as such -- never as a note, whatever its
// extension, and never as a notebook. See `library::kFilesDirName`.
MICRONOTES_TEST(library_walk_files_directory_contents_are_companions_not_notes) {
  const micronotes::tests::TempDir rootDir("micronotes-companions-walk-test");
  const auto& root = rootDir.path();
  const auto touch = [&](const std::filesystem::path& relative) {
    std::filesystem::create_directories((root / relative).parent_path());
    std::ofstream out(root / relative);
    out << "x";
  };
  touch("Inbox.md");
  touch("files/report.pdf");
  touch("work/Alpha.md");
  touch("work/files/diagram.png");
  touch("work/files/sub/clip.mp4");
  // A note-shaped file in a files area is a file: micronotes does not read it.
  touch("work/files/notes.md");
  // A nested `files` is an ordinary folder inside the area, not a second rule.
  touch("work/files/files/nested.txt");
  // The match is exact and case-sensitive, so this is a notebook.
  touch("Files/Beta.md");
  // A durable write's staging file is debris wherever it is found.
  touch("work/files/.diagram.png.microcore-write.123.1");

  micronotes::library::Library library(root);
  std::vector<std::filesystem::directory_entry> notes;
  std::vector<std::filesystem::path> directories;
  std::vector<micronotes::library::CompanionEntry> companions;
  library.walk(&notes, &directories, &companions);

  std::vector<std::string> noteNames;
  for(const auto& entry : notes) noteNames.push_back(entry.path().lexically_relative(root).generic_string());
  std::sort(noteNames.begin(), noteNames.end());
  MICRONOTES_REQUIRE((noteNames == std::vector<std::string> {"Files/Beta.md", "Inbox.md", "work/Alpha.md"}));

  std::vector<std::string> directoryNames;
  for(const auto& dir : directories) directoryNames.push_back(dir.generic_string());
  std::sort(directoryNames.begin(), directoryNames.end());
  MICRONOTES_REQUIRE((directoryNames == std::vector<std::string> {"Files", "work"}));

  std::vector<std::string> companionNames;
  for(const auto& entry : companions) {
    companionNames.push_back(entry.path.generic_string() + (entry.directory ? "/" : ""));
    MICRONOTES_REQUIRE(entry.folder == entry.path.parent_path());
  }
  std::sort(companionNames.begin(), companionNames.end());
  MICRONOTES_REQUIRE((companionNames == std::vector<std::string> {
    "files/", "files/report.pdf", "work/files/", "work/files/diagram.png", "work/files/files/",
    "work/files/files/nested.txt", "work/files/notes.md", "work/files/sub/", "work/files/sub/clip.mp4"}));

  // The rule, asked directly.
  using micronotes::library::filesRootOf;
  using micronotes::library::insideFilesDir;
  using micronotes::library::isFilesDir;
  MICRONOTES_REQUIRE(isFilesDir("files"));
  MICRONOTES_REQUIRE(isFilesDir("work/files"));
  MICRONOTES_REQUIRE(isFilesDir("work/files/"));
  MICRONOTES_REQUIRE(!isFilesDir("work/files/sub"));
  MICRONOTES_REQUIRE(!isFilesDir("work/files/files"));
  MICRONOTES_REQUIRE(!isFilesDir("Files"));
  MICRONOTES_REQUIRE(!isFilesDir("work"));
  MICRONOTES_REQUIRE(!isFilesDir(""));
  MICRONOTES_REQUIRE(insideFilesDir("work/files"));
  MICRONOTES_REQUIRE(insideFilesDir("work/files/sub/clip.mp4"));
  MICRONOTES_REQUIRE(!insideFilesDir("work/Alpha.md"));
  MICRONOTES_REQUIRE(!insideFilesDir("Files/Beta.md"));
  MICRONOTES_REQUIRE(filesRootOf("work/files/sub/clip.mp4") == std::filesystem::path("work/files"));
  MICRONOTES_REQUIRE(filesRootOf("files/report.pdf") == std::filesystem::path("files"));
  MICRONOTES_REQUIRE(filesRootOf("work/Alpha.md").empty());

  // One files directory on its own, for the watcher: the same entries the whole
  // walk reported for it, and nothing from any other.
  const auto one = library.walkFilesDir("work/files");
  std::vector<std::string> oneNames;
  for(const auto& entry : one) oneNames.push_back(entry.path.generic_string() + (entry.directory ? "/" : ""));
  std::sort(oneNames.begin(), oneNames.end());
  MICRONOTES_REQUIRE((oneNames == std::vector<std::string> {
    "work/files/", "work/files/diagram.png", "work/files/files/", "work/files/files/nested.txt",
    "work/files/notes.md", "work/files/sub/", "work/files/sub/clip.mp4"}));
  MICRONOTES_REQUIRE(library.walkFilesDir("work").empty());
  MICRONOTES_REQUIRE(library.walkFilesDir("ideas/files").empty());
}

MICRONOTES_TEST(library_companion_goes_to_the_trash_and_comes_back_with_its_extension) {
  const micronotes::tests::TempDir rootDir("micronotes-companions-trash-test");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root / "work" / "files");
  { std::ofstream out(root / "work" / "files" / "diagram.png"); out << "png"; }
  micronotes::library::Library library(root);

  // Not a companion: refused, and nothing moves.
  MICRONOTES_REQUIRE(!library.deleteCompanion("work"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work"));

  MICRONOTES_REQUIRE(library.deleteCompanion("work/files/diagram.png"));
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "work" / "files" / "diagram.png"));
  const auto entries = library.trashEntries();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries[0].title == "diagram.png");
  MICRONOTES_REQUIRE(entries[0].originalRelative == std::filesystem::path("work/files/diagram.png"));
  MICRONOTES_REQUIRE(library.restoreFromTrash(entries[0].name));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "files" / "diagram.png"));

  // The whole files directory can go, as one entry, and come back.
  MICRONOTES_REQUIRE(library.deleteCompanion("work/files"));
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "work" / "files"));
  const auto after = library.trashEntries();
  MICRONOTES_REQUIRE(after.size() == 1);
  MICRONOTES_REQUIRE(library.restoreFromTrash(after[0].name));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "files" / "diagram.png"));
}

MICRONOTES_TEST(library_companion_move_refuses_the_anchor_and_a_move_into_itself) {
  const micronotes::tests::TempDir rootDir("micronotes-companions-move-test");
  const auto& root = rootDir.path();
  const auto touch = [&](const std::filesystem::path& relative) {
    std::filesystem::create_directories((root / relative).parent_path());
    std::ofstream out(root / relative);
    out << "x";
  };
  touch("work/files/sub/clip.mp4");
  touch("files/a.pdf");
  touch("files/b.pdf");
  micronotes::library::Library library(root);

  // The `files` directory is the convention; renaming it would take every file
  // in it out of the tree at once.
  MICRONOTES_REQUIRE(library.moveCompanion("work/files", "work/stuff").empty());
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "files"));
  // A folder cannot become its own child.
  MICRONOTES_REQUIRE(library.moveCompanion("work/files/sub", "work/files/sub/inner").empty());
  // Neither end may leave a files area: out there it would be a note or a notebook.
  MICRONOTES_REQUIRE(library.moveCompanion("files/a.pdf", "work/a.pdf").empty());
  MICRONOTES_REQUIRE(library.moveCompanion("work/Alpha.md", "files/Alpha.md").empty());

  // Across notebooks is fine, and the target's parent is made on the way.
  const auto moved = library.moveCompanion("work/files/sub", "ideas/files/sub");
  MICRONOTES_REQUIRE(moved == root / "ideas" / "files" / "sub");
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "ideas" / "files" / "sub" / "clip.mp4"));
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "work" / "files" / "sub"));

  // A rename onto a taken name numbers itself rather than overwriting, and the
  // extension survives the numbering.
  const auto renamed = library.moveCompanion("files/a.pdf", "files/b.pdf");
  MICRONOTES_REQUIRE(!renamed.empty());
  MICRONOTES_REQUIRE(renamed != root / "files" / "b.pdf");
  MICRONOTES_REQUIRE(renamed.extension() == ".pdf");
  MICRONOTES_REQUIRE(std::filesystem::exists(renamed));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "files" / "b.pdf"));
  // And a rename to the name it already has is a no-op, not a `-2`.
  MICRONOTES_REQUIRE(library.moveCompanion("files/b.pdf", "files/b.pdf") == root / "files" / "b.pdf");
}
