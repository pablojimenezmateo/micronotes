#include "app/Commands.h"

#include "app/BlockMenus.h"
#include "app/Chrome.h"
#include "app/ContextMenus.h"
#include "app/EditCommands.h"
#include "app/FocusedEdits.h"
#include "app/Fields.h"
#include "app/FindBar.h"
#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/RightPanel.h"
#include "app/SessionState.h"
#include "app/SettingsPane.h"
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
  ui.state.editWorkspace().setPaneMode(mode);
  ui.focus = mode == ui::PaneMode::Viewer ? FocusArea::Viewer : FocusArea::Editor;
  ui.revealEditorCursor = true;
  ui.status = paneModeName(mode);
}

void cyclePaneMode(UiRuntime& ui) {
  switch(ui.paneMode()) {
    case ui::PaneMode::Editor: setPaneMode(ui, ui::PaneMode::Viewer); break;
    case ui::PaneMode::Viewer: setPaneMode(ui, ui::PaneMode::Split); break;
    case ui::PaneMode::Split: setPaneMode(ui, ui::PaneMode::Editor); break;
  }
}


// Opens a different library without restarting. The one being left is written
// out first, so its open note and its pinned notes go with it rather than
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
  if(ui.state.catalog().isOpen() && std::filesystem::equivalent(root, ui.state.catalog().root(), ec) && !ec) {
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
  else if(id == "new-note") beginNoteCreate(ui);
  else if(id == "new-folder") beginFolderCreate(ui);
  else if(id == "save") saveCurrent(ui);
  else if(id == "rename") beginRename(ui);
  else if(id == "icon") openIconPicker(ui);
  else if(id == "tags") beginTagEdit(ui);
  else if(id == "pin-note") {
    const auto noteId = ui.state.selection().noteId;
    if(noteId.empty()) ui.status = "No note selected";
    else ui.status = ui.state.editWorkspace().togglePinned(noteId) ? "Pinned note" : "Unpinned note";
  }
  // Show on disk / copy relative / copy absolute, about the note on the page.
  else if(handleNotePathCommand(ui, id, {})) {}
  else if(id == "move-note") openFolderPalette(ui);
  else if(id == "move-blocks") openNotePalette(ui, "move-blocks-target", "Move blocks to");
  else if(id == "delete-note") openDeleteNoteConfirm(ui);
  else if(id == "rename-folder") beginFolderRename(ui);
  else if(id == "delete-folder") openDeleteFolderConfirm(ui);
  else if(id == "restore") openTrashPalette(ui);
  else if(id == "theme") {
    ui::setThemeMode(ui::themeMode() == ui::ThemeMode::Light ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = ui::themeMode() == ui::ThemeMode::Light ? "Light theme" : "Dark theme";
  }
  else if(id == "settings") openSettingsSurface(ui);
  else if(id == "about") openAboutSurface(ui);
  // Through the same pending action the close button raises, so the one place
  // that knows how to shut the window down -- flushing the open note and the
  // library's state on the way out -- stays the only one.
  else if(id == "quit") ui.chrome.pendingWindowAction = WindowAction::Close;
  // The key reference is the About page: see `aboutRows`.
  else if(id == "shortcuts") openAboutSurface(ui);
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
  // Through the same function the key runs, because these two are the case
  // where having a second implementation was already wrong: this one handled
  // the note buffer and not a focused field, so choosing Undo from the Edit
  // menu with the caret in a rename box did nothing while Ctrl+Z worked.
  else if(id == "undo") undoInFocus(ui);
  else if(id == "redo") redoInFocus(ui);
  // And the other five of the six, through the same functions the keys run,
  // for the same reason: a second implementation here is what made choosing
  // Undo from the menu with the caret in a rename box do nothing.
  else if(id == "cut") cutSelectionInFocus(ui);
  else if(id == "copy") copySelectionInFocus(ui);
  else if(id == "paste") pasteInFocus(ui, /*plainTextOnly=*/false);
  else if(id == "paste-plain") pasteInFocus(ui, /*plainTextOnly=*/true);
  else if(id == "select-all") selectAllInFocus(ui);
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
  else if(id == "pane-raw") setPaneMode(ui, ui::PaneMode::Editor);
  else if(id == "pane-reading") setPaneMode(ui, ui::PaneMode::Viewer);
  else if(id == "pane-split") setPaneMode(ui, ui::PaneMode::Split);
  else if(id == "cycle-pane") cyclePaneMode(ui);
  else if(id == "find") openFindInNote(ui);
  else if(id == "find-next") moveFindMatch(ui, 1);
  else if(id == "find-previous") moveFindMatch(ui, -1);
  else if(id == "find-match-case") toggleFindOption(ui, FindToggle::MatchCase);
  else if(id == "find-whole-word") toggleFindOption(ui, FindToggle::WholeWord);
  else if(id == "search") focusSearchAllNotes(ui);
  else if(id == "toggle-sidebar") togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar");
  else if(id == "toggle-right") togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Outline panel");
  else if(id == "cycle-right") cycleRightPanel(ui);
  else if(id == "next-tab") stepTab(ui, 1);
  else if(id == "previous-tab") stepTab(ui, -1);
  else if(id == "close-tab") closeActiveTab(ui);
  else if(id == "close-other-tabs") closeTabsAroundActive(ui, TabCloseScope::Others);
  else if(id == "close-tabs-right") closeTabsAroundActive(ui, TabCloseScope::ToRight);
  else if(id == "close-tabs-left") closeTabsAroundActive(ui, TabCloseScope::ToLeft);
  else if(id == "close-all-tabs") closeTabsAroundActive(ui, TabCloseScope::All);
  else if(id == "new-tab") openNotePalette(ui, "jump-note-new", "Open in a new tab");
  else if(id == "pin-tab") {
    if(auto* tab = ui.state.editWorkspace().activeTab_()) {
      tab->pinned = !tab->pinned;
      ui.status = tab->pinned ? "Tab pinned" : "Tab unpinned";
    }
  }
}

}
