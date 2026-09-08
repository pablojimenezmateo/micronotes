#include "app/Commands.h"

#include "app/BlockMenus.h"
#include "app/Chrome.h"
#include "app/ContextMenus.h"
#include "app/EditCommands.h"
#include "app/Fields.h"
#include "app/Folds.h"
#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/RightPanel.h"
#include "app/SessionState.h"
#include "app/SettingsDialog.h"
#include "app/Shell.h"
#include "app/TabStrip.h"
#include "app/WikiLinks.h"
#include "core/platform/PathUtils.h"
#include "doc/Edits.h"
#include "ui/Theme.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace micronotes::app {

void setPaneMode(UiRuntime& ui, ui::PaneMode mode) {
  ui.blockSelection.clear();
  ui.state.workspace().setPaneMode(mode);
  ui.focus = mode == ui::PaneMode::Viewer ? FocusArea::Viewer : FocusArea::Editor;
  ui.revealEditorCursor = true;
  ui.status = paneModeName(mode);
}

void cyclePaneMode(UiRuntime& ui) {
  switch(ui.state.workspace().paneMode()) {
    case ui::PaneMode::Live: setPaneMode(ui, ui::PaneMode::Editor); break;
    case ui::PaneMode::Editor: setPaneMode(ui, ui::PaneMode::Viewer); break;
    case ui::PaneMode::Viewer: setPaneMode(ui, ui::PaneMode::Split); break;
    case ui::PaneMode::Split: setPaneMode(ui, ui::PaneMode::Live); break;
  }
}


// Opens a different library without restarting. The one being left is written
// out first, so its open note, favorites and folds go with it rather than
// following the user into the new one.
void switchLibrary(UiRuntime& ui, const std::string& typed) {
  std::string value = typed;
  while(!value.empty() && (value.back() == ' ' || value.back() == '/')) value.pop_back();
  if(value.empty()) {
    ui.status = "Give a folder to open";
    return;
  }
  // `~` is the shell's, and nothing expanded it on the way into a text field.
  if(value == "~" || value.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    if(!home || !*home) {
      ui.status = "No HOME to expand ~ against";
      return;
    }
    value = std::string(home) + value.substr(1);
  }
  const std::filesystem::path root(value);
  std::error_code ec;
  if(ui.state.hasLibrary() && std::filesystem::equivalent(root, ui.state.libraryRoot(), ec) && !ec) {
    ui.status = "Already open";
    return;
  }
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty()) (void)saveCurrent(ui, true);
  persistLibraryState(ui);
  try {
    if(!openLibraryRoot(ui, root)) {
      ui.status = "Could not open " + platform::displayPath(root);
      return;
    }
  } catch(const std::exception& error) {
    ui.status = "Could not open " + platform::displayPath(root) + ": " + error.what();
    return;
  }
  writeConfiguredLibraryRoot(root);
  ui.status = "Opened " + platform::displayPath(root);
}

void performCommand(UiRuntime& ui, const std::string& id) {
  if(id == "jump") openNotePalette(ui, "jump-note", "Go to note");
  else if(id == "command-palette") openCommandPalette(ui);
  else if(id == "new-note") createNote(ui);
  else if(id == "new-folder") beginFolderCreate(ui);
  else if(id == "save") saveCurrent(ui);
  else if(id == "rename") beginRename(ui);
  else if(id == "icon") openIconPicker(ui);
  else if(id == "tags") beginTagEdit(ui);
  else if(id == "favorite") {
    const auto noteId = ui.state.selection().noteId;
    if(noteId.empty()) ui.status = "No note selected";
    else ui.status = ui.state.toggleFavorite(noteId) ? "Added to favorites" : "Removed from favorites";
  }
  // Show on disk / copy relative / copy absolute, about the note on the page.
  else if(handleNotePathCommand(ui, id, {})) {}
  else if(id == "move-note") openFolderPalette(ui);
  else if(id == "move-blocks") {
    if(!ui.blockSelection.active) ui.status = "Select blocks first with Esc";
    else openNotePalette(ui, "move-blocks-target", "Move blocks to");
  }
  else if(id == "delete-note") openDeleteNoteConfirm(ui);
  else if(id == "rename-folder") beginFolderRename(ui);
  else if(id == "delete-folder") openDeleteFolderConfirm(ui);
  else if(id == "restore") openTrashPalette(ui);
  else if(id == "fold") toggleFoldAt(ui, ui.editor.cursor());
  else if(id == "theme") {
    ui::setThemeMode(ui::themeMode() == ui::ThemeMode::Light ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = ui::themeMode() == ui::ThemeMode::Light ? "Light theme" : "Dark theme";
  }
  else if(id == "settings") openSettings(ui);
  // Through the same pending action the close button raises, so the one place
  // that knows how to shut the window down -- flushing the open note and the
  // library's state on the way out -- stays the only one.
  else if(id == "quit") ui.chrome.pendingWindowAction = WindowAction::Close;
  else if(id == "shortcuts") openShortcutHelp(ui);
  // The editing verbs. They used to be reachable only from the key chain, on
  // the reasoning that a palette row for one is useless -- the palette has
  // taken the keyboard away from the editor, so there is no selection left to
  // embolden. That is still true of the palette and it is why these rows carry
  // `inPalette = false`; it is not true of the menu bar, which leaves
  // `ui.focus` where it was, so the caret and the selection are still there
  // when the item is chosen.
  else if(id == "bold") wrapEditorSelection(ui, "**", "**", "Bold");
  else if(id == "italic") wrapEditorSelection(ui, "*", "*", "Italic");
  else if(id == "code") wrapEditorSelection(ui, "`", "`", "Code");
  else if(id == "link") linkEditorSelection(ui);
  else if(id == "undo") {
    if(ui.focus == FocusArea::Editor) (void)undoEditorEdit(ui);
  }
  else if(id == "redo") {
    if(ui.focus == FocusArea::Editor) (void)redoEditorEdit(ui);
  }
  else if(id == "toggle-task") {
    if(ui.focus != FocusArea::Editor) ui.status = "Put the caret in a task first";
    else if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
  }
  else if(id == "turn-into") openTurnIntoMenu(ui, ui.pointer.x, ui.pointer.y);
  else if(id == "insert-block") openSlashMenu(ui, ui.editor.cursor());
  else if(id == "duplicate-block") performBlockCommand(ui, "duplicate");
  else if(id == "delete-block") performBlockCommand(ui, "delete");
  else if(id == "move-block-up") performBlockCommand(ui, "move-up");
  else if(id == "move-block-down") performBlockCommand(ui, "move-down");
  else if(id == "refresh") {
    invalidateWikiNotes(ui);
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  }
  else if(id == "pane-live") setPaneMode(ui, ui::PaneMode::Live);
  else if(id == "pane-raw") setPaneMode(ui, ui::PaneMode::Editor);
  else if(id == "pane-reading") setPaneMode(ui, ui::PaneMode::Viewer);
  else if(id == "pane-split") setPaneMode(ui, ui::PaneMode::Split);
  else if(id == "cycle-pane") cyclePaneMode(ui);
  else if(id == "find") focusFindInNote(ui);
  else if(id == "search") focusSearchAllNotes(ui);
  else if(id == "toggle-sidebar") togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar");
  else if(id == "toggle-right") togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Outline panel");
  else if(id == "cycle-right") cycleRightPanel(ui);
  else if(id == "next-tab") stepTab(ui, 1);
  else if(id == "previous-tab") stepTab(ui, -1);
  else if(id == "close-tab") closeActiveTab(ui);
  else if(id == "new-tab") openNotePalette(ui, "jump-note-new", "Open in a new tab");
  else if(id == "pin-tab") {
    auto& workspace = ui.state.workspace();
    if(auto* tab = workspace.activeTab_()) {
      tab->pinned = !tab->pinned;
      ui.status = tab->pinned ? "Tab pinned" : "Tab unpinned";
    }
  }
}

}
