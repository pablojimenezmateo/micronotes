#include "app/FocusedEdits.h"

#include "app/Clipboard.h"
#include "app/Desktop.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Fields.h"
#include "app/Shell.h"
#include "doc/BlockScan.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace micronotes::app {

void selectAllInFocus(UiRuntime& ui) {
  if(ui.focus == FocusArea::Editor) {
    ui.editor.selectAll();
    publishEditorPrimarySelection(ui);
    ui.revealEditorCursor = true;
  }
  else if(auto* field = focusedField(ui)) {
    field->editor.selectAll();
    if(field->editor.hasSelection()) SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
  }
}

namespace {

// The source the selected blocks cover, whole. `blockSelectionCarets` gives two
// offsets inside the first and last block; the text is from the start of one to
// the end of the other, so a copy takes whole blocks rather than the fragment
// between two carets.
std::string_view selectedBlockText(const UiRuntime& ui) {
  const auto [from, to] = blockSelectionCarets(ui);
  const EditorBlocks blocks(ui);
  const std::size_t start = blocks[doc::blockIndexAt(blocks, from)].start;
  const std::size_t end = blocks[doc::blockIndexAt(blocks, to)].end();
  return std::string_view(ui.editor.text()).substr(start, end - start);
}

bool blocksAreSelected(const UiRuntime& ui) {
  return ui.focus == FocusArea::Editor && ui.blockSelection.active;
}

// Whether a cut has to stop, having reported why.
//
// A cut that could not reach the clipboard must not erase anything. All three
// arms used to copy, erase unconditionally, and then *say* the copy had failed
// -- so a compositor that refused the selection left the text gone from the
// note and absent from the clipboard, recoverable only by knowing to press
// undo. Copy has no such branch: a failed copy changes nothing by construction,
// which is what cut now matches.
bool cutFailed(UiRuntime& ui, bool copied) {
  if(copied) return false;
  ui.status = "Cut failed: " + std::string(SDL_GetError());
  return true;
}

}

void copySelectionInFocus(UiRuntime& ui) {
  if(blocksAreSelected(ui)) {
    ui.status = setClipboardText(selectedBlockText(ui))
                  ? "Copied block" : "Copy failed: " + std::string(SDL_GetError());
  } else if(readsTheNote(ui.focus) && ui.editor.hasSelection()) {
    // `Viewer` as well as `Editor`: the reading pane makes a selection in the
    // same buffer, and a selection you can see and cannot copy is worse than
    // one you cannot make.
    ui.status = setClipboardText(ui.editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
  } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
    // Copies the actual selected range. It used to copy the whole field,
    // because the whole field was the only range that could be selected.
    ui.status = setClipboardText(field->editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
  }
}

void cutSelectionInFocus(UiRuntime& ui) {
  // Blocks first, and for the same reason copy takes them first: with a block
  // selection up there is no editor selection at all -- `selectBlockAtCursor`
  // clears it -- so without this arm Ctrl+X fell through every branch and did
  // nothing, silently, while Ctrl+C copied the blocks.
  if(blocksAreSelected(ui)) {
    if(!cutFailed(ui, setClipboardText(selectedBlockText(ui)))) {
      // The same deletion `performBlockCommand("delete")` runs, so cut is copy
      // and delete rather than a third opinion about what a block selection is.
      performBlockCommand(ui, "delete");
      ui.status = "Cut block";
    }
  } else if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
    if(!cutFailed(ui, setClipboardText(ui.editor.selectedText()))) {
      ui.editor.eraseSelection();
      ui.markEdited();
      ui.revealEditorCursor = true;
      ui.status = "Cut selection";
    }
  } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
    if(!cutFailed(ui, setClipboardText(field->editor.selectedText()))) {
      field->editor.eraseSelection();
      syncFocusedInput(ui);
      ui.status = "Cut selection";
    }
  }
}

void undoInFocus(UiRuntime& ui) {
  if(auto* field = focusedField(ui)) {
    if(field->editor.undo()) syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    (void)undoEditorEdit(ui);
  }
}

void redoInFocus(UiRuntime& ui) {
  if(auto* field = focusedField(ui)) {
    if(field->editor.redo()) syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    (void)redoEditorEdit(ui);
  }
}

void pasteInFocus(UiRuntime& ui, bool plainTextOnly) {
  if(focusedField(ui)) {
    pasteClipboardIntoInput(ui);
    return;
  }
  if(ui.focus != FocusArea::Editor) return;
  // Decide by what is actually on the clipboard: image data wins (an image
  // copy often also exposes an incidental text/plain target), otherwise paste
  // text. Shift forces plain text even when an image is present.
  if(plainTextOnly) {
    if(!pasteClipboardText(ui)) pasteClipboardImage(ui);
  } else {
    if(!pasteClipboardImage(ui)) pasteClipboardText(ui);
  }
  ui.revealEditorCursor = true;
}

}
