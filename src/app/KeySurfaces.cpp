#include "app/KeySurfaces.h"

#include "app/Clipboard.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Fields.h"
#include "app/Notes.h"
#include "app/PageView.h"
#include "app/Prompts.h"
#include "app/RawPane.h"
#include "app/Shell.h"
#include "app/SidebarModel.h"
#include "core/editor/TextField.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"

#include <algorithm>
#include <cstddef>

namespace micronotes::app {

// Enter is the only key whose meaning depends on which of the five fields this
// is; everything else -- arrows, word motion, Home/End, Backspace, Delete,
// Ctrl+Z/Y -- is the same for all of them and lives in one place.
void handleFieldKey(UiRuntime& ui, editor::TextField& field, SDL_Keycode key, bool ctrl,
                    bool shift) {
  if(key == SDLK_RETURN) {
    switch(ui.focus) {
      case FocusArea::TagEditor: saveTags(ui); break;
      case FocusArea::RenameNote: saveRename(ui); break;
      case FocusArea::RenameFolder: saveFolderRename(ui); break;
      default: ui.focus = FocusArea::Editor; break;
    }
    return;
  }
  const auto result = editor::applyKeyToField(field, key, ctrl, shift);
  if(result == editor::FieldKeyResult::Changed) syncFocusedInput(ui);
  else if(result == editor::FieldKeyResult::Moved && field.editor.hasSelection()) {
    SDL_SetPrimarySelectionText(field.editor.selectedText().c_str());
  }
}

// Selected blocks are objects: the arrows walk them, and one command acts over
// the whole range.
void handleBlockSelectionKey(UiRuntime& ui, SDL_Keycode key, bool shift, bool alt) {
  if(alt && (key == SDLK_UP || key == SDLK_DOWN)) {
    moveSelectedBlocks(ui, key == SDLK_UP ? -1 : 1);
  } else if(key == SDLK_UP || key == SDLK_DOWN) {
    moveBlockSelection(ui, key == SDLK_UP ? -1 : 1, shift);
  } else if(key == SDLK_BACKSPACE || key == SDLK_DELETE) {
    performBlockCommand(ui, "delete");
  } else if(key == SDLK_TAB) {
    applyTransform(ui, shift ? doc::outdent : doc::indent);
    syncBlockSelectionToEdit(ui);
  } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
    // Enter puts the caret back into the first selected block's text.
    const EditorBlocks blocks(ui);
    const auto content = blocks[doc::blockIndexAt(blocks, blockSelectionCarets(ui).first)].contentStart();
    ui.blockSelection.clear();
    ui.editor.moveCursor(content);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_LEFT || key == SDLK_RIGHT) {
    ui.blockSelection.clear();
  }
}

void handleEditorKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift, bool alt) {
  const bool live = ui.state.workspace().paneMode() == ui::PaneMode::Live;
  // One visual row up or down: the live surface wraps, the raw editor does not.
  const auto rowStep = [&](int rows) {
    return live ? ui.livePage.rowRelative(ui.editor.cursor(), rows) : ui.editor.cursor();
  };
  if(key == SDLK_BACKSPACE) {
    if(ctrl) ui.editor.erasePreviousWord();
    // Against a block's first character, Backspace strips the block's marker
    // before it starts eating the block above.
    else if(ui.editor.hasSelection() || !applyTransform(ui, doc::outdentOrUnwrap)) ui.editor.erasePrevious();
    ui.markEdited();
    ui.revealEditorCursor = true;
  } else if(key == SDLK_DELETE) {
    if(ctrl) ui.editor.eraseNextWord();
    else ui.editor.eraseNext();
    ui.markEdited();
    ui.revealEditorCursor = true;
  } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
    if(ctrl) {
      if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
    } else if(ui.editor.hasSelection() ||
              (!applyTransform(ui, doc::closeFence) && !applyTransform(ui, doc::continueList))) {
      ui.editor.insert("\n");
      ui.markEdited();
      ui.revealEditorCursor = true;
    }
  } else if(key == SDLK_TAB) {
    if(shift) {
      applyTransform(ui, doc::outdent);
    } else if(!applyTransform(ui, doc::indent)) {
      ui.editor.insert("  ");
      ui.markEdited();
      ui.revealEditorCursor = true;
    }
  } else if(key == SDLK_LEFT) {
    if(ctrl) ui.editor.moveWordLeft(shift);
    else ui.editor.moveLeft(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_RIGHT) {
    if(ctrl) ui.editor.moveWordRight(shift);
    else ui.editor.moveRight(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_UP) {
    if(alt) moveSelectedBlocks(ui, -1);
    else if(live) ui.editor.moveTo(rowStep(-1), shift);
    else ui.editor.moveLineUp(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_DOWN) {
    if(alt) moveSelectedBlocks(ui, 1);
    else if(live) ui.editor.moveTo(rowStep(1), shift);
    else ui.editor.moveLineDown(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
    const int direction = key == SDLK_PAGEUP ? -1 : 1;
    if(live) {
      const int rows = static_cast<int>(std::max<std::size_t>(1, ui.livePage.rowsPerPage()));
      ui.editor.moveTo(rowStep(direction * rows), shift);
    } else {
      for(int i = 0; i < std::max(1, ui.raw.visibleRows); ++i) {
        if(direction < 0) ui.editor.moveLineUp(shift);
        else ui.editor.moveLineDown(shift);
      }
    }
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_HOME) {
    if(ctrl) ui.editor.moveDocumentStart(shift);
    else ui.editor.moveLineStart(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  } else if(key == SDLK_END) {
    if(ctrl) ui.editor.moveDocumentEnd(shift);
    else ui.editor.moveLineEnd(shift);
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  }
}

void handleSidebarKey(UiRuntime& ui, SDL_Keycode key) {
  if(key == SDLK_DOWN || key == SDLK_UP) moveTreeCursor(ui, key == SDLK_DOWN ? 1 : -1);
  else if(key == SDLK_RIGHT || key == SDLK_LEFT) expandTreeCursor(ui, key == SDLK_RIGHT);
  // Enter chooses the row the cursor is on; see `chooseSidebarCursorRow`.
  else if(key == SDLK_RETURN) ui.focus = chooseSidebarCursorRow(ui);
}

}
