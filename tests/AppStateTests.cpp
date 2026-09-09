#include "TestSupport.h"
#include "TempDir.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "ui/AppState.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// A write that does not go through micronotes: what another editor, a sync
// daemon or a `git checkout` looks like from in here.
void writeExternally(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
  out.close();
  // The disk signature is mtime plus size, and a test can rewrite a file inside
  // one filesystem tick with the same length. Nudging the mtime is what makes
  // the change as visible to the app as a human's edit would be.
  const auto later = std::filesystem::last_write_time(path) + std::chrono::seconds(2);
  std::filesystem::last_write_time(path, later);
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

}

MICRONOTES_TEST(app_state_opens_library_filters_and_persists_state) {
  const micronotes::tests::TempDir rootDir("micronotes-app-state-test");
  const auto& root = rootDir.path();

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-1";
  metadata.title = "Find Me";
  metadata.tags = {"tagged"};
  const auto path = library.createNote(metadata, "search body");
  library.moveNote(path, "folder");

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  state.selectFolder("folder");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  state.selectTag("tagged");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  state.setSearch("search");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  state.selectNote("note-1");
  state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Viewer);
  const auto statePath = root / ".micronotes" / "ui.state";
  MICRONOTES_REQUIRE(state.saveUiState(statePath));

  micronotes::ui::AppState loaded;
  MICRONOTES_REQUIRE(loaded.loadUiState(statePath));
  MICRONOTES_REQUIRE(loaded.selection().noteId == "note-1");
  MICRONOTES_REQUIRE(loaded.workspace().paneMode() == micronotes::ui::PaneMode::Viewer);
}

MICRONOTES_TEST(app_state_creates_loads_saves_and_refreshes_notes) {
  const micronotes::tests::TempDir rootDir("micronotes-app-state-crud-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  auto created = state.createNote("Untitled", "work", "# Untitled\n\nbody");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.selection().noteId == created->id);
  MICRONOTES_REQUIRE(state.catalog().folders().size() == 1);
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);

  auto loaded = state.openNote().read();
  MICRONOTES_REQUIRE(loaded.has_value());
  MICRONOTES_REQUIRE(loaded->body.find("body") != std::string::npos);
  auto duplicate = state.createNote("Untitled", "work", "# Untitled\n\nsecond body");
  MICRONOTES_REQUIRE(duplicate.has_value());
  MICRONOTES_REQUIRE(duplicate->id != created->id);
  MICRONOTES_REQUIRE(duplicate->title == "Untitled-2");
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "Untitled.md"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "Untitled-2.md"));
  state.selectNote(created->id);
  auto originalAfterDuplicate = state.openNote().read();
  MICRONOTES_REQUIRE(originalAfterDuplicate.has_value());
  MICRONOTES_REQUIRE(originalAfterDuplicate->body.find("body") != std::string::npos);
  MICRONOTES_REQUIRE(originalAfterDuplicate->body.find("second body") == std::string::npos);
  state.selectNote(duplicate->id);
  MICRONOTES_REQUIRE(state.deleteSelectedNote());
  state.selectNote(created->id);
  const std::string body = "# Body heading\n\nchanged searchable text";
  MICRONOTES_REQUIRE(state.saveSelectedNote(body).ok);
  auto saved = state.openNote().read();
  MICRONOTES_REQUIRE(saved.has_value());
  MICRONOTES_REQUIRE(saved->metadata.title == "Untitled");
  MICRONOTES_REQUIRE(saved->body.find("# Body heading") == 0);
  state.setSearch("searchable");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  MICRONOTES_REQUIRE(state.updateSelectedTags({"fast", "local"}, body));
  state.selectTag("fast");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  auto retagged = state.openNote().read();
  MICRONOTES_REQUIRE(retagged.has_value());
  MICRONOTES_REQUIRE(retagged->metadata.tags.size() == 2);
  MICRONOTES_REQUIRE(state.renameSelectedNote("Renamed", body));
  auto renamed = state.openNote().read();
  MICRONOTES_REQUIRE(renamed.has_value());
  MICRONOTES_REQUIRE(renamed->metadata.title == "Renamed");
  MICRONOTES_REQUIRE(renamed->body.find("# Body heading") == 0);
  state.selectFolder("work");
  MICRONOTES_REQUIRE(state.renameSelectedFolder("archive"));
  MICRONOTES_REQUIRE(state.selection().folder == "archive");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  MICRONOTES_REQUIRE(state.deleteSelectedFolder());
  MICRONOTES_REQUIRE(state.currentNotes().empty());
  MICRONOTES_REQUIRE(state.createFolder("ideas"));
  MICRONOTES_REQUIRE(state.selection().folder == "ideas");
  auto movable = state.createNote("Move Me", "ideas", "move body");
  MICRONOTES_REQUIRE(movable.has_value());
  MICRONOTES_REQUIRE(state.createFolder("archive"));
  state.selectNote(movable->id);
  MICRONOTES_REQUIRE(state.moveSelectedNoteToFolder("archive"));
  MICRONOTES_REQUIRE(state.selection().folder == "archive");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);

}


MICRONOTES_TEST(app_state_recovers_unsaved_selected_note_body) {
  const micronotes::tests::TempDir rootDir("micronotes-recovery-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  auto created = state.createNote("Recover", {}, "saved body");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.openNote().saveRecovery("draft body"));
  auto recovered = state.openNote().recoveryBody();
  MICRONOTES_REQUIRE(recovered.has_value());
  MICRONOTES_REQUIRE(*recovered == "draft body");

  MICRONOTES_REQUIRE(state.saveSelectedNote("draft body").ok);
  MICRONOTES_REQUIRE(!state.openNote().recoveryBody().has_value());
  auto saved = state.openNote().read();
  MICRONOTES_REQUIRE(saved.has_value());
  MICRONOTES_REQUIRE(saved->body == "draft body");

}


// The core of "I do not want to lose data": a save must not overwrite a change
// it did not make.
MICRONOTES_TEST(app_state_keeps_an_external_change_instead_of_overwriting_it) {
  const micronotes::tests::TempDir rootDir("micronotes-external-change-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  const auto created = state.createNote("Shared", {}, "mine\n");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.saveSelectedNote("mine\n").ok);
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Agrees);

  // Somebody else rewrites the file: a second editor, a checkout, a sync.
  writeExternally(created->path, "---\nid: " + created->id + "\ntitle: Shared\n---\n\ntheirs\n");
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Changed);

  const auto result = state.saveSelectedNote("mine, edited\n");
  MICRONOTES_REQUIRE(result.ok);
  // The buffer landed, so typing was never blocked...
  MICRONOTES_REQUIRE(readFile(created->path).find("mine, edited") != std::string::npos);
  // ...and their text was kept rather than destroyed.
  MICRONOTES_REQUIRE(!result.conflictCopy.empty());
  const auto kept = created->path.parent_path() / result.conflictCopy;
  MICRONOTES_REQUIRE(std::filesystem::exists(kept));
  MICRONOTES_REQUIRE(readFile(kept).find("theirs") != std::string::npos);
  // It is a note in the library, not a hidden file, and it has an identity of
  // its own -- two notes sharing an id would be one row in the index.
  MICRONOTES_REQUIRE(kept.extension() == ".md");
  const auto conflictId = micronotes::library::Library(root).loadNote(kept).metadata.id;
  MICRONOTES_REQUIRE(!conflictId.empty() && conflictId != created->id);
  MICRONOTES_REQUIRE(state.catalog().notes().size() == 2);

  // And the save has agreed with the file again, so the next one is ordinary.
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Agrees);
  MICRONOTES_REQUIRE(state.saveSelectedNote("mine, again\n").conflictCopy.empty());

}

MICRONOTES_TEST(app_state_reloads_a_note_that_changed_on_disk) {
  const micronotes::tests::TempDir rootDir("micronotes-reload-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  const auto created = state.createNote("Reload", {}, "first\n");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.saveSelectedNote("first\n").ok);

  writeExternally(created->path,
                  "---\nid: " + created->id + "\ntitle: Reload\ntags: added\n---\n\nsecond\n");
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Changed);
  MICRONOTES_REQUIRE(state.reloadSelectedNote());

  // The body, the front matter and the index all followed.
  const auto reloaded = state.openNote().read();
  MICRONOTES_REQUIRE(reloaded.has_value());
  MICRONOTES_REQUIRE(reloaded->body == "second\n");
  MICRONOTES_REQUIRE(state.openNote().metadata().tags.size() == 1);
  MICRONOTES_REQUIRE(state.openNote().metadata().tags.front() == "added");
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Agrees);
  state.setSearch("second");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  state.setSearch("first");
  MICRONOTES_REQUIRE(state.currentNotes().empty());

}

// The front matter's `id` is what everything points at, so a note whose id
// changes on disk must not be allowed to disappear out from under its reader.
MICRONOTES_TEST(app_state_follows_a_note_whose_id_changed_on_disk) {
  const micronotes::tests::TempDir rootDir("micronotes-reload-id-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  const auto created = state.createNote("Repointed", {}, "body\n");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.saveSelectedNote("body\n").ok);
  MICRONOTES_REQUIRE(state.workspace().tabs.size() == 1);

  writeExternally(created->path, "---\nid: brand-new-id\ntitle: Repointed\n---\n\nbody\n");
  MICRONOTES_REQUIRE(state.reloadSelectedNote());
  MICRONOTES_REQUIRE(state.selection().noteId == "brand-new-id");
  MICRONOTES_REQUIRE(state.workspace().tabs.front().noteId == "brand-new-id");
  MICRONOTES_REQUIRE(state.openNote().read().has_value());
  MICRONOTES_REQUIRE(state.catalog().notes().size() == 1);

}

// A note another tool wrote has no front matter, so the library files it under
// an id derived from its path. The first save gives it a permanent one -- and
// used to leave the selection, the tab and the favorites pointing at the id it
// no longer has, so the note vanished the moment you saved it.
MICRONOTES_TEST(app_state_carries_the_selection_when_a_foreign_note_adopts_an_id) {
  const micronotes::tests::TempDir rootDir("micronotes-adopt-id-test");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  writeExternally(root / "Foreign.md", "just markdown, no front matter\n");

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  MICRONOTES_REQUIRE(state.catalog().notes().size() == 1);
  const auto pathId = state.catalog().notes().front().id;
  state.selectNote(pathId);
  MICRONOTES_REQUIRE(state.editWorkspace().toggleFavorite(pathId));

  MICRONOTES_REQUIRE(state.saveSelectedNote("edited by micronotes\n").ok);
  MICRONOTES_REQUIRE(state.selection().noteId != pathId);
  MICRONOTES_REQUIRE(!state.selection().noteId.empty());
  // Still open, still selected, still favorite, and still one note.
  MICRONOTES_REQUIRE(state.openNote().read().has_value());
  MICRONOTES_REQUIRE(state.workspace().isFavorite(state.selection().noteId));
  MICRONOTES_REQUIRE(state.workspace().tabs.front().noteId == state.selection().noteId);
  MICRONOTES_REQUIRE(state.catalog().notes().size() == 1);

}

// A note whose file is deleted underneath the app keeps its buffer: the save
// writes it back rather than the note quietly ceasing to exist.
MICRONOTES_TEST(app_state_writes_a_vanished_note_back) {
  const micronotes::tests::TempDir rootDir("micronotes-vanished-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  const auto created = state.createNote("Vanishing", {}, "body\n");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.saveSelectedNote("body\n").ok);

  std::filesystem::remove(created->path);
  MICRONOTES_REQUIRE(state.openNote().diskState() == micronotes::ui::DiskState::Vanished);
  const auto result = state.saveSelectedNote("body still here\n");
  MICRONOTES_REQUIRE(result.ok);
  // Nothing was filed aside, because there was nothing there to keep.
  MICRONOTES_REQUIRE(result.conflictCopy.empty());
  MICRONOTES_REQUIRE(readFile(created->path).find("body still here") != std::string::npos);

}

MICRONOTES_TEST(app_state_numbers_a_rename_onto_a_name_already_taken) {
  // A note is a file, so two notes in one folder cannot both be called TODO.
  // Nothing blocks the rename: the second is numbered, and the caller is
  // expected to notice that the title it got back is not the one it asked for.
  const micronotes::tests::TempDir rootDir("micronotes-rename-collision-test");
  const auto& root = rootDir.path();

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  MICRONOTES_REQUIRE(state.createNote("TODO", "pi4", "first").has_value());
  const auto second = state.createNote("Scratch", "pi4", "second");
  MICRONOTES_REQUIRE(second.has_value());

  MICRONOTES_REQUIRE(state.renameSelectedNote("TODO", "second"));
  MICRONOTES_REQUIRE(state.selectedTitle() == "TODO-2");
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "pi4" / "TODO.md"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "pi4" / "TODO-2.md"));

  // And renaming a note to the name it already has is not a collision with
  // itself: the numbering must not creep every time the prompt is confirmed.
  MICRONOTES_REQUIRE(state.renameSelectedNote("TODO-2", "second"));
  MICRONOTES_REQUIRE(state.selectedTitle() == "TODO-2");
}

// A files directory is not a notebook, and the app refuses to treat it as one:
// no note lands in it, no notebook takes its name, and a notebook cannot be
// moved into it. The companion operations, meanwhile, refuse the moves that
// would lose a file. See `library::kFilesDirName`.
MICRONOTES_TEST(app_state_keeps_notes_and_notebooks_out_of_a_files_area) {
  const micronotes::tests::TempDir rootDir("micronotes-app-state-companions-test");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root / "work" / "files");
  std::filesystem::create_directories(root / "other");
  { std::ofstream out(root / "work" / "files" / "a.pdf"); out << "pdf"; }

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  MICRONOTES_REQUIRE(state.catalog().companions().size() == 2);

  MICRONOTES_REQUIRE(!state.createNote("Stray", "work/files").has_value());
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "work" / "files" / "Stray.md"));
  MICRONOTES_REQUIRE(!state.createFolder("files"));
  MICRONOTES_REQUIRE(!std::filesystem::exists(root / "files"));
  MICRONOTES_REQUIRE(!state.createFolder("work/files/sub"));
  MICRONOTES_REQUIRE(!state.moveFolderInto("other", "work/files"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "other"));
  state.selectFolder("other");
  MICRONOTES_REQUIRE(!state.renameSelectedFolder("files"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "other"));
  MICRONOTES_REQUIRE(state.selection().folder == std::filesystem::path("other"));

  // The companion side of the same rule.
  MICRONOTES_REQUIRE(state.renameCompanion("work/files", "stuff").empty());
  MICRONOTES_REQUIRE(state.moveCompanion("work/files/a.pdf", "work/files").empty());
  MICRONOTES_REQUIRE(state.renameCompanion("work/files/a.pdf", "../a.pdf").empty());
  const auto before = state.catalog().revision();
  const auto renamed = state.renameCompanion("work/files/a.pdf", "b.pdf");
  MICRONOTES_REQUIRE(renamed == root / "work" / "files" / "b.pdf");
  MICRONOTES_REQUIRE(state.catalog().revision() > before);
  bool listed = false;
  for(const auto& entry : state.catalog().companions()) {
    if(entry.path == std::filesystem::path("work/files/b.pdf")) listed = true;
    MICRONOTES_REQUIRE(entry.path != std::filesystem::path("work/files/a.pdf"));
  }
  MICRONOTES_REQUIRE(listed);
  // Moved to another notebook, whose files directory did not exist yet.
  const auto moved = state.moveCompanion("work/files/b.pdf", "other/files");
  MICRONOTES_REQUIRE(moved == root / "other" / "files" / "b.pdf");
  bool inOther = false;
  for(const auto& entry : state.catalog().companions()) {
    if(entry.path == std::filesystem::path("other/files/b.pdf")) inOther = true;
  }
  MICRONOTES_REQUIRE(inOther);
  // A folder inside a files area is made through the companion door, and the
  // notebook door still refuses it.
  MICRONOTES_REQUIRE(state.createCompanionFolder("other/sub").empty());
  MICRONOTES_REQUIRE(state.createCompanionFolder("other/files/sub") == root / "other" / "files" / "sub");
  MICRONOTES_REQUIRE(state.deleteCompanion("other/files/b.pdf"));
  MICRONOTES_REQUIRE(state.catalog().trashEntries().size() == 1);
}
