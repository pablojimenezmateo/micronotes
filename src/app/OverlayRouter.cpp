#include "app/OverlayRouter.h"

#include "app/BlockMenus.h"
#include "app/Commands.h"
#include "app/EditCommands.h"
#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/SettingsDialog.h"
#include "app/Shell.h"
#include "app/TabStrip.h"
#include "app/WikiLinks.h"
#include "ui/Settings.h"
#include "ui/Theme.h"

#include <filesystem>
#include <string>

namespace micronotes::app {

void handleOverlayResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId == "rename-note") {
    ui.rename.beginWith(result.value, false);
    saveRename(ui);
  } else if(result.overlayId == "tags") {
    ui.tag.beginWith(result.value, false);
    saveTags(ui);
  } else if(result.overlayId == "folder-name") {
    ui.folderRename.beginWith(result.value, false);
    saveFolderRename(ui);
  } else if(result.overlayId == "delete-note") {
    deleteSelected(ui);
  } else if(result.overlayId == "delete-folder") {
    deleteSelectedFolder(ui);
  } else if(result.overlayId == "note-menu") {
    if(result.itemId == "new") createNote(ui);
    else if(result.itemId == "rename") beginRename(ui);
    else if(result.itemId == "delete") openDeleteNoteConfirm(ui);
    // The rest are the palette's, so the menu and the palette cannot drift.
    else if(result.itemId == "move") performCommand(ui, "move-note");
    else performCommand(ui, result.itemId);
  } else if(handleTabMenuResult(ui, result)) {
    // Close, pin, and the three path commands, about the tab it was opened on.
  } else if(handleTagOverlayResult(ui, result)) {
    // Filter, colour, un-colour: all three are verbs on the tag, so they live
    // with the others in `app/Notes.h`.
  } else if(result.overlayId == "block-menu") {
    if(result.itemId == "turn") openTurnIntoMenu(ui, ui.mouseX, ui.mouseY);
    else performBlockCommand(ui, result.itemId);
  } else if(result.overlayId == "turn-into") {
    performBlockCommand(ui, result.itemId);
  } else if(result.overlayId == "slash-menu") {
    commitSlashMenu(ui, result.itemId);
  } else if(result.overlayId == "wiki-menu") {
    commitWikiMenu(ui, result.itemId, result.value);
  } else if(result.overlayId == "command-palette") {
    performCommand(ui, result.itemId);
  } else if(result.overlayId == "jump-note-new") {
    if(saveCurrent(ui, true)) {
      ui.state.selectNote(result.itemId);
      loadSelectedIntoEditor(ui);
    }
  } else if(result.overlayId == "jump-note") {
    selectNoteById(ui, result.itemId);
    if(const auto note = ui.state.findNote(result.itemId)) {
      ui.search.reset();
      showFolder(ui, note->folder);
      ui.state.selectNote(result.itemId);
    }
    ui.focus = FocusArea::Editor;
  } else if(result.overlayId == "move-blocks-target") {
    moveBlocksToNote(ui, result.itemId);
  } else if(result.overlayId == "move-note-folder") {
    const std::filesystem::path folder = result.itemId == "/" ? std::filesystem::path {} : std::filesystem::path {result.itemId};
    ui.status = ui.state.moveSelectedNoteToFolder(folder) ? "Moved note" : "Move note failed";
    ui.tree.reveal(folder);
  } else if(result.overlayId == "restore-trash") {
    ui.status = ui.state.restoreFromTrash(result.itemId) ? "Restored from trash" : "Restore failed";
  } else if(result.overlayId == "note-icon") {
    ui.status = ui.state.setSelectedNoteIcon(result.itemId)
                  ? (result.itemId.empty() ? "Removed icon" : "Set icon")
                  : "Could not set icon";
  } else if(result.overlayId == "settings") {
    if(result.itemId == "library") openLibraryPrompt(ui);
    else if(result.itemId == "shortcuts") openShortcutHelp(ui);
    else openSettingsValues(ui, result.itemId);
  } else if(result.overlayId == "settings-theme") {
    ui::setThemeMode(result.itemId == "light" ? ui::ThemeMode::Light : ui::ThemeMode::Dark);
    // The list comes back with the new value on it: changing two settings
    // should not need the dialog opened twice.
    openSettings(ui);
  } else if(result.overlayId == "settings-text-size") {
    ui::setTextSize(ui::textSizeFromName(result.itemId));
    openSettings(ui);
  } else if(result.overlayId == "settings-page-width") {
    ui::setPageWidth(ui::pageWidthFromName(result.itemId));
    openSettings(ui);
  } else if(result.overlayId == "settings-library") {
    switchLibrary(ui, result.value);
  } else if(result.overlayId == "folder-menu") {
    if(result.itemId == "new-folder") beginFolderCreate(ui);
    else if(result.itemId == "new-note") createNoteInFolder(ui, ui.state.selection().folder);
    else if(result.itemId == "rename") beginFolderRename(ui);
    else if(result.itemId == "delete") openDeleteFolderConfirm(ui);
  }
}

}
