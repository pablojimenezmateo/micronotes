#include "app/OverlayRouter.h"

#include "app/BlockMenus.h"
#include "app/Commands.h"
#include "app/EditCommands.h"
#include "app/ExportPdf.h"
#include "app/Notes.h"
#include "app/Prompts.h"
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
    ui.fields.rename.beginWith(result.value, false);
    saveRename(ui);
  } else if(result.overlayId == "new-note-name") {
    saveNoteCreate(ui, result.value);
  } else if(result.overlayId == "tags") {
    ui.fields.tag.beginWith(result.value, false);
    saveTags(ui);
  } else if(result.overlayId == "folder-name") {
    ui.fields.folderRename.beginWith(result.value, false);
    saveFolderRename(ui);
  } else if(result.overlayId == "delete-note") {
    deleteSelected(ui);
  } else if(result.overlayId == "delete-folder") {
    deleteSelectedFolder(ui);
  } else if(result.overlayId == "note-menu") {
    if(result.itemId == "new") beginNoteCreate(ui);
    else if(result.itemId == "rename") beginRename(ui);
    else if(result.itemId == "delete") openDeleteNoteConfirm(ui);
    else if(result.itemId == "export-pdf") exportNoteToPdf(ui, ui.state.selection().noteId);
    // The rest are the palette's, so the menu and the palette cannot drift.
    else if(result.itemId == "move") performCommand(ui, "move-note");
    else performCommand(ui, result.itemId);
  } else if(handleTabMenuResult(ui, result)) {
    // Close, pin, and the three path commands, about the tab it was opened on.
  } else if(handleTagOverlayResult(ui, result)) {
    // Filter, colour, un-colour: all three are verbs on the tag, so they live
    // with the others in `app/Notes.h`.
  } else if(result.overlayId == "block-menu") {
    if(result.itemId == "turn") openTurnIntoMenu(ui, ui.pointer.x, ui.pointer.y);
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
    if(const auto note = ui.state.catalog().findNote(result.itemId)) {
      ui.fields.search.reset();
      showFolder(ui, note->folder);
      ui.state.selectNote(result.itemId);
    }
    ui.focus = FocusArea::Editor;
  } else if(result.overlayId == "move-blocks-target") {
    moveBlocksToNote(ui, result.itemId);
  } else if(result.overlayId == "move-note-folder") {
    const std::filesystem::path folder = result.itemId == "/" ? std::filesystem::path {} : std::filesystem::path {result.itemId};
    ui.status = ui.state.moveSelectedNoteToFolder(folder) ? "Moved note" : "Move note failed";
    ui.sidebar.tree.reveal(folder);
  } else if(result.overlayId == "restore-trash") {
    ui.status = ui.state.restoreFromTrash(result.itemId) ? "Restored from trash" : "Restore failed";
  } else if(result.overlayId == "note-icon") {
    ui.status = ui.state.setSelectedNoteIcon(result.itemId, ui.editor.text())
                  ? (result.itemId.empty() ? "Removed icon" : "Set icon")
                  : "Could not set icon";
  } else if(result.overlayId == "settings-library") {
    switchLibrary(ui, result.value);
  } else if(result.overlayId == "companion-name") {
    saveCompanionRename(ui, result.value);
  } else if(result.overlayId == "companion-folder-name") {
    saveCompanionFolderCreate(ui, result.value);
  } else if(result.overlayId == "delete-companion") {
    deleteCompanionTarget(ui);
  } else if(result.overlayId == "move-companion-folder") {
    const std::filesystem::path folder = result.itemId == "/" ? std::filesystem::path {} : std::filesystem::path {result.itemId};
    moveCompanionToNotebook(ui, folder);
  } else if(result.overlayId == "file-menu") {
    if(result.itemId == "open") openCompanion(ui, ui.sidebar.companionTarget);
    else if(result.itemId == "rename") beginCompanionRename(ui);
    else if(result.itemId == "move") openCompanionMovePalette(ui);
    else if(result.itemId == "delete") openDeleteCompanionConfirm(ui);
    else if(handleCompanionPathCommand(ui, result.itemId, ui.sidebar.companionTarget)) {}
  } else if(result.overlayId == "files-folder-menu") {
    if(result.itemId == "new-folder") beginCompanionFolderCreate(ui);
    else if(result.itemId == "rename") beginCompanionRename(ui);
    else if(result.itemId == "delete") openDeleteCompanionConfirm(ui);
    else if(handleCompanionPathCommand(ui, result.itemId, ui.sidebar.companionTarget)) {}
  } else if(result.overlayId == "folder-menu") {
    if(result.itemId == "new-folder") beginFolderCreate(ui);
    else if(result.itemId == "new-note") createNoteInFolder(ui, ui.state.selection().folder);
    else if(result.itemId == "rename") beginFolderRename(ui);
    else if(result.itemId == "delete") openDeleteFolderConfirm(ui);
    else if(result.itemId == "export-pdf") exportFolderToPdf(ui, ui.state.selection().folder);
    else if(handleFolderPathCommand(ui, result.itemId, ui.state.selection().folder)) {}
  }
}

}
