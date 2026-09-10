#include "TestSupport.h"
#include "ShellFixture.h"

#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/OverlayRouter.h"
#include "app/Shell.h"
#include "app/TabStrip.h"
#include "library/Library.h"
#include "ui/Overlay.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Every row of every context menu does something.
//
// The menu bar has had this check since it was written -- an action the bar
// offers and nothing dispatches fails the build. The context menus had none,
// and they are the surface where it is easiest to lose one: their rows carry
// bare string ids rather than `ui::ActionId`s, and each menu's arm of
// `handleOverlayResult` ends in a fall-through that quietly swallows anything
// it does not recognise. Renaming one id from `export-pdf` to
// `export-folder-pdf` was enough to make "Export as PDF" on a notebook do
// nothing at all, with every other test still green.

namespace {

using micronotes::app::UiRuntime;

// What "something happened" is, for a menu row: everything about the shell a
// menu row could plausibly move, as one string.
//
// Deliberately broad, because the rows are. One puts a prompt up, one reports
// in the status line, one narrows the selection, one closes a tab and says
// nothing at all -- and a witness narrower than the union of those reads a row
// that worked as a row that did not. What it must never miss is a row that
// moved nothing, which is the only thing this is trying to catch.
std::string witnessOf(const UiRuntime& ui) {
  const auto& workspace = ui.state.workspace();
  const auto& selection = ui.state.selection();
  std::string out;
  out += "status=" + ui.status.text;
  out += "|overlay=" + std::string(ui.overlays.top() != nullptr ? ui.overlays.top()->id : "");
  out += "|window=" + std::to_string(static_cast<int>(ui.chrome.pendingWindowAction));
  out += "|tabs=" + std::to_string(workspace.tabs.size());
  out += "|active=" + std::to_string(workspace.activeTab);
  out += "|note=" + selection.noteId;
  out += "|folder=" + selection.folder.generic_string();
  out += "|tag=" + selection.tag;
  out += "|pane=" + std::to_string(static_cast<int>(workspace.paneMode()));
  out += "|pinned=" + std::to_string(workspace.pinnedNotes.size());
  out += "|colours=" + std::to_string(workspace.tagColors.choices().size());
  out += "|text=" + ui.editor.text();
  return out;
}

// The rows a menu offers, in the order it offers them. Opening it is the only
// way to ask: the tables are locals inside `app/ContextMenus.cpp`.
std::vector<std::string> rowsOf(UiRuntime& ui) {
  std::vector<std::string> ids;
  const auto* overlay = ui.overlays.top();
  micronotes::tests::require(overlay != nullptr, "the menu did not open");
  for(const auto& item : overlay->items) {
    if(item.separator || item.id.empty()) continue;
    ids.push_back(item.id);
  }
  ui.overlays.close();
  return ids;
}

// Runs each row of a menu on a shell that has just been put back the way it
// started, and requires that each one moved something.
void everyRowDoesSomething(UiRuntime& ui, const std::string& overlayId,
                           const std::vector<std::string>& ids,
                           const std::vector<std::string>& allowedToDoNothing = {}) {
  micronotes::tests::require(!ids.empty(), overlayId + " offered no rows at all");
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    ui.chrome.pendingWindowAction = micronotes::app::WindowAction::None;
    const std::string before = witnessOf(ui);

    micronotes::app::handleOverlayResult(ui, {overlayId, id, ""});

    const bool expected = std::find(allowedToDoNothing.begin(), allowedToDoNothing.end(), id) ==
                          allowedToDoNothing.end();
    const bool moved = witnessOf(ui) != before;
    micronotes::tests::require(
      moved == expected,
      overlayId + "'s \"" + id + "\" row " + (moved ? "did something unexpectedly" : "did nothing") +
        " -- a row that runs no command, opens nothing and says nothing is a row "
        "that looks broken and is");
  }
  while(ui.overlays.active()) ui.overlays.close();
}

}

MICRONOTES_TEST(context_menu_every_note_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-note", "# Note\n\nBody\n");
  micronotes::app::openNoteMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "note-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_notebook_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-folder", "# Note\n\nBody\n");
  micronotes::app::openFolderMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "folder-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_tab_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-tab", "# Note\n\nBody\n");
  const auto noteId = ui.state.selection().noteId;
  MICRONOTES_REQUIRE(!noteId.empty());
  micronotes::app::openTabMenu(ui, noteId, 100.0f, 100.0f);
  const auto ids = rowsOf(ui);
  // The tab menu carries which tab it is about in `value`, so the rows have to
  // be run with it rather than through the plain helper.
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    const std::string before = witnessOf(ui);
    micronotes::app::handleOverlayResult(ui, {"tab-menu", id, noteId});
    micronotes::tests::require(witnessOf(ui) != before,
                               "the tab menu's \"" + id + "\" row did nothing");
    // Closing a tab is the one row that leaves the fixture without the note
    // the rest are about, so it is run last by being restored here.
    if(ui.state.workspace().tabs.empty()) micronotes::app::selectNoteById(ui, noteId);
  }
}

MICRONOTES_TEST(context_menu_every_tag_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-tag", "---\ntags: [work]\n---\n\nBody\n");
  micronotes::app::openTagMenu(ui, "work", 100.0f, 100.0f);
  const auto ids = rowsOf(ui);
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    const std::string before = witnessOf(ui);
    micronotes::app::handleOverlayResult(ui, {"tag-menu", id, "work"});
    micronotes::tests::require(witnessOf(ui) != before,
                               "the tag menu's \"" + id + "\" row did nothing");
  }
}

// The two menus over the `files` tree. They share the shape of the note's and
// the notebook's but not one line of their routing, and their target is a path
// on `ui.sidebar` rather than the selection -- which is exactly the kind of
// second copy that goes stale unnoticed.
MICRONOTES_TEST(context_menu_every_companion_file_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-file", "# Note\n\nBody\n");
  const auto files = scratch.root() / std::string(micronotes::library::kFilesDirName);
  std::filesystem::create_directories(files);
  { std::ofstream out(files / "diagram.svg"); out << "<svg/>\n"; }
  ui.state.refreshLibrary();

  ui.sidebar.companionTarget = std::filesystem::path(micronotes::library::kFilesDirName) / "diagram.svg";
  micronotes::app::openFileMenu(ui, 100.0f, 100.0f);
  // "Open" hands the file to the desktop, which on a test machine with no
  // handler reports either way -- both are a status line, which is the
  // witness. Nothing else here touches disk.
  everyRowDoesSomething(ui, "file-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_companion_folder_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-files-dir", "# Note\n\nBody\n");
  const auto files = scratch.root() / std::string(micronotes::library::kFilesDirName);
  std::filesystem::create_directories(files / "diagrams");
  ui.state.refreshLibrary();

  ui.sidebar.companionTarget = std::filesystem::path(micronotes::library::kFilesDirName) / "diagrams";
  micronotes::app::openFilesFolderMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "files-folder-menu", rowsOf(ui));
}
