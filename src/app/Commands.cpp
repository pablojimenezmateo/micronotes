#include "app/Commands.h"

#include "app/BlockMenus.h"
#include "app/Chrome.h"
#include "app/ContextMenus.h"
#include "app/EditCommands.h"
#include "app/ExportPdf.h"
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
#include <iterator>
#include <span>
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

namespace {

// Every command, once. See `CommandSpec` for why this is a table.
constexpr CommandSpec kCommands[] = {
  {"jump", [](UiRuntime& ui) { openNotePalette(ui, "jump-note", "Go to note"); }},
  {"command-palette", [](UiRuntime& ui) { openCommandPalette(ui); }},
  {"new-note", [](UiRuntime& ui) { beginNoteCreate(ui); }},
  {"new-folder", [](UiRuntime& ui) { beginFolderCreate(ui); }},
  {"save", [](UiRuntime& ui) { saveCurrent(ui); }},
  {"rename", [](UiRuntime& ui) { beginRename(ui); }},
  {"icon", [](UiRuntime& ui) { openIconPicker(ui); }},
  {"tags", [](UiRuntime& ui) { beginTagEdit(ui); }},
  {"pin-note", [](UiRuntime& ui) {
    const auto noteId = ui.state.selection().noteId;
    if(noteId.empty()) ui.status = "No note selected";
    else ui.status = ui.state.editWorkspace().togglePinned(noteId) ? "Pinned note" : "Unpinned note";
  }},
  // Show on disk / copy relative / copy absolute, about the note on the page.
  //
  // Three rows through one helper, which used to tell them apart by the id it
  // was *handed* -- so the literal appeared twice per row, and a row passing
  // its neighbour's spelling dispatched, did something, and did the wrong
  // thing. Both spellings are real commands, so nothing structural could see
  // it, and the two copies are indistinguishable to a test with no clipboard.
  // `PathCommand` removes the failure rather than checking for it.
  {"show-on-disk",
   [](UiRuntime& ui) { handleNotePathCommand(ui, PathCommand::ShowOnDisk, {}); }},
  {"copy-relative-path",
   [](UiRuntime& ui) { handleNotePathCommand(ui, PathCommand::CopyRelative, {}); }},
  {"copy-absolute-path",
   [](UiRuntime& ui) { handleNotePathCommand(ui, PathCommand::CopyAbsolute, {}); }},
  {"move-note", [](UiRuntime& ui) { openFolderPalette(ui); }},
  {"move-blocks", [](UiRuntime& ui) { openNotePalette(ui, "move-blocks-target", "Move blocks to"); }},
  {"delete-note", [](UiRuntime& ui) { openDeleteNoteConfirm(ui); }},
  {"export-note-pdf", [](UiRuntime& ui) { exportNoteToPdf(ui, ui.state.selection().noteId); }},
  {"export-folder-pdf", [](UiRuntime& ui) { exportFolderToPdf(ui, ui.state.selection().folder); }},
  {"rename-folder", [](UiRuntime& ui) { beginFolderRename(ui); }},
  {"delete-folder", [](UiRuntime& ui) { openDeleteFolderConfirm(ui); }},
  {"restore", [](UiRuntime& ui) { openTrashPalette(ui); }},
  {"theme", [](UiRuntime& ui) {
    ui::setThemeMode(ui::themeMode() == ui::ThemeMode::Light ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = ui::themeMode() == ui::ThemeMode::Light ? "Light theme" : "Dark theme";
  }},
  {"settings", [](UiRuntime& ui) { openSettingsSurface(ui); }},
  {"about", [](UiRuntime& ui) { openAboutSurface(ui); }},
  // Through the same pending action the close button raises, so the one place
  // that knows how to shut the window down -- flushing the open note and the
  // library's state on the way out -- stays the only one.
  {"quit", [](UiRuntime& ui) { ui.chrome.pendingWindowAction = WindowAction::Close; }},
  // The key reference is the About page: see `aboutRows`.
  {"shortcuts", [](UiRuntime& ui) { openAboutSurface(ui); }},
  // The editing verbs. They used to be reachable only from the key chain, on
  // the reasoning that a palette row for one is useless -- the palette has
  // taken the keyboard away from the editor, so there is no selection left to
  // embolden. That is still true of the palette and it is why these rows carry
  // `inPalette = false`; it is not true of the menu bar, which leaves
  // `ui.focus` where it was, so the caret and the selection are still there
  // when the item is chosen.
  {"bold", [](UiRuntime& ui) { wrapEditorSelection(ui, "**", "**", "Bold"); }},
  {"italic", [](UiRuntime& ui) { wrapEditorSelection(ui, "*", "*", "Italic"); }},
  {"code", [](UiRuntime& ui) { wrapEditorSelection(ui, "`", "`", "Code"); }},
  {"link", [](UiRuntime& ui) { linkEditorSelection(ui); }},
  // Through the same function the key runs, because these two are the case
  // where having a second implementation was already wrong: this one handled
  // the note buffer and not a focused field, so choosing Undo from the Edit
  // menu with the caret in a rename box did nothing while Ctrl+Z worked.
  {"undo", [](UiRuntime& ui) { undoInFocus(ui); }},
  {"redo", [](UiRuntime& ui) { redoInFocus(ui); }},
  // And the other five of the six, through the same functions the keys run,
  // for the same reason: a second implementation here is what made choosing
  // Undo from the menu with the caret in a rename box do nothing.
  {"cut", [](UiRuntime& ui) { cutSelectionInFocus(ui); }},
  {"copy", [](UiRuntime& ui) { copySelectionInFocus(ui); }},
  {"paste", [](UiRuntime& ui) { pasteInFocus(ui, /*plainTextOnly=*/false); }},
  {"paste-plain", [](UiRuntime& ui) { pasteInFocus(ui, /*plainTextOnly=*/true); }},
  {"select-all", [](UiRuntime& ui) { selectAllInFocus(ui); }},
  {"toggle-task", [](UiRuntime& ui) {
    if(ui.focus != FocusArea::Editor) ui.status = "Put the caret in a task first";
    else if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
  }},
  {"turn-into", [](UiRuntime& ui) { openTurnIntoMenu(ui); }},
  {"insert-block", [](UiRuntime& ui) { openSlashMenu(ui, ui.editor.cursor()); }},
  {"duplicate-block", [](UiRuntime& ui) { performBlockCommand(ui, "duplicate"); }},
  {"delete-block", [](UiRuntime& ui) { performBlockCommand(ui, "delete"); }},
  {"move-block-up", [](UiRuntime& ui) { performBlockCommand(ui, "move-up"); }},
  {"move-block-down", [](UiRuntime& ui) { performBlockCommand(ui, "move-down"); }},
  {"refresh", [](UiRuntime& ui) {
    invalidateWikiNotes(ui);
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  }},
  {"pane-raw", [](UiRuntime& ui) { setPaneMode(ui, ui::PaneMode::Editor); }},
  {"pane-reading", [](UiRuntime& ui) { setPaneMode(ui, ui::PaneMode::Viewer); }},
  {"pane-split", [](UiRuntime& ui) { setPaneMode(ui, ui::PaneMode::Split); }},
  {"cycle-pane", [](UiRuntime& ui) { cyclePaneMode(ui); }},
  {"find", [](UiRuntime& ui) { openFindInNote(ui); }},
  {"find-next", [](UiRuntime& ui) { moveFindMatch(ui, 1); }},
  {"find-previous", [](UiRuntime& ui) { moveFindMatch(ui, -1); }},
  {"find-match-case", [](UiRuntime& ui) { toggleFindOption(ui, FindToggle::MatchCase); }},
  {"find-whole-word", [](UiRuntime& ui) { toggleFindOption(ui, FindToggle::WholeWord); }},
  {"search", [](UiRuntime& ui) { focusSearchAllNotes(ui); }},
  {"toggle-sidebar", [](UiRuntime& ui) { togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar"); }},
  {"toggle-right", [](UiRuntime& ui) { togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Right panel"); }},
  {"cycle-right", [](UiRuntime& ui) { cycleRightPanel(ui); }},
  {"next-tab", [](UiRuntime& ui) { stepTab(ui, 1); }},
  {"previous-tab", [](UiRuntime& ui) { stepTab(ui, -1); }},
  {"close-tab", [](UiRuntime& ui) { closeActiveTab(ui); }},
  {"close-other-tabs", [](UiRuntime& ui) { closeTabsAroundActive(ui, TabCloseScope::Others); }},
  {"close-tabs-right", [](UiRuntime& ui) { closeTabsAroundActive(ui, TabCloseScope::ToRight); }},
  {"close-tabs-left", [](UiRuntime& ui) { closeTabsAroundActive(ui, TabCloseScope::ToLeft); }},
  {"close-all-tabs", [](UiRuntime& ui) { closeTabsAroundActive(ui, TabCloseScope::All); }},
  {"new-tab", [](UiRuntime& ui) { openNotePalette(ui, "jump-note-new", "Open in a new tab"); }},
  {"pin-tab", [](UiRuntime& ui) {
    if(auto* tab = ui.state.editWorkspace().activeTab_()) {
      tab->pinned = !tab->pinned;
      ui.status = tab->pinned ? "Tab pinned" : "Tab unpinned";
    }
  }},
};

}

std::span<const CommandSpec> commandSpecs() {
  return std::span<const CommandSpec>(kCommands, std::size(kCommands));
}

// A linear walk, not a map. Ninety string compares that mostly fail on their
// first byte, run once per click or keystroke; a hash table here would be a
// structure to keep in step with the table for a cost nothing can measure.
void performCommand(UiRuntime& ui, const std::string& id) {
  for(const CommandSpec& command : kCommands) {
    if(command.name != id) continue;
    command.run(ui);
    return;
  }
}

}
