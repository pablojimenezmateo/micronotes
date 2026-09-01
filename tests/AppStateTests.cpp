#include "TestSupport.h"

#include "library/Library.h"
#include "library/Metadata.h"
#include "ui/AppState.h"

#include <filesystem>
#include <string>

MICRONOTES_TEST(app_state_opens_library_filters_and_persists_state) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-app-state-test";
  std::filesystem::remove_all(root);

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
  state.workspace().paneMode = micronotes::ui::PaneMode::Viewer;
  const auto statePath = root / ".micronotes" / "ui.state";
  MICRONOTES_REQUIRE(state.saveUiState(statePath));

  micronotes::ui::AppState loaded;
  MICRONOTES_REQUIRE(loaded.loadUiState(statePath));
  MICRONOTES_REQUIRE(loaded.selection().noteId == "note-1");
  MICRONOTES_REQUIRE(loaded.workspace().paneMode == micronotes::ui::PaneMode::Viewer);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(app_state_creates_loads_saves_and_refreshes_notes) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-app-state-crud-test";
  std::filesystem::remove_all(root);

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  auto created = state.createNote("Untitled", "work", "# Untitled\n\nbody");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.selection().noteId == created->id);
  MICRONOTES_REQUIRE(state.folders().size() == 1);
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);

  auto loaded = state.selectedNote();
  MICRONOTES_REQUIRE(loaded.has_value());
  MICRONOTES_REQUIRE(loaded->body.find("body") != std::string::npos);
  auto duplicate = state.createNote("Untitled", "work", "# Untitled\n\nsecond body");
  MICRONOTES_REQUIRE(duplicate.has_value());
  MICRONOTES_REQUIRE(duplicate->id != created->id);
  MICRONOTES_REQUIRE(duplicate->title == "Untitled-2");
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "Untitled.md"));
  MICRONOTES_REQUIRE(std::filesystem::exists(root / "work" / "Untitled-2.md"));
  state.selectNote(created->id);
  auto originalAfterDuplicate = state.selectedNote();
  MICRONOTES_REQUIRE(originalAfterDuplicate.has_value());
  MICRONOTES_REQUIRE(originalAfterDuplicate->body.find("body") != std::string::npos);
  MICRONOTES_REQUIRE(originalAfterDuplicate->body.find("second body") == std::string::npos);
  state.selectNote(duplicate->id);
  MICRONOTES_REQUIRE(state.deleteSelectedNote());
  state.selectNote(created->id);
  MICRONOTES_REQUIRE(state.saveSelectedNote("# Body heading\n\nchanged searchable text"));
  auto saved = state.selectedNote();
  MICRONOTES_REQUIRE(saved.has_value());
  MICRONOTES_REQUIRE(saved->metadata.title == "Untitled");
  MICRONOTES_REQUIRE(saved->body.find("# Body heading") == 0);
  state.setSearch("searchable");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  MICRONOTES_REQUIRE(state.updateSelectedTags({"fast", "local"}));
  state.selectTag("fast");
  MICRONOTES_REQUIRE(state.currentNotes().size() == 1);
  auto retagged = state.selectedNote();
  MICRONOTES_REQUIRE(retagged.has_value());
  MICRONOTES_REQUIRE(retagged->metadata.tags.size() == 2);
  MICRONOTES_REQUIRE(state.renameSelectedNote("Renamed"));
  auto renamed = state.selectedNote();
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

  std::filesystem::remove_all(root);
}


MICRONOTES_TEST(app_state_recovers_unsaved_selected_note_body) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-recovery-test";
  std::filesystem::remove_all(root);

  micronotes::ui::AppState state;
  MICRONOTES_REQUIRE(state.openOrCreateLibrary(root));
  auto created = state.createNote("Recover", {}, "saved body");
  MICRONOTES_REQUIRE(created.has_value());
  MICRONOTES_REQUIRE(state.saveSelectedNoteRecovery("draft body"));
  auto recovered = state.selectedRecoveryBody();
  MICRONOTES_REQUIRE(recovered.has_value());
  MICRONOTES_REQUIRE(*recovered == "draft body");

  MICRONOTES_REQUIRE(state.saveSelectedNote("draft body"));
  MICRONOTES_REQUIRE(!state.selectedRecoveryBody().has_value());
  auto saved = state.selectedNote();
  MICRONOTES_REQUIRE(saved.has_value());
  MICRONOTES_REQUIRE(saved->body == "draft body");

  std::filesystem::remove_all(root);
}

