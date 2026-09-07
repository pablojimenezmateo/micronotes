#include "app/EditCommands.h"

#include "app/Notes.h"

#include <algorithm>

namespace micronotes::app {

// Block transforms arrive as one erase-and-insert, so they land on the editor's
// single undo stack instead of keeping state of their own.
bool applyEdit(UiRuntime& ui, const doc::Edit& edit) {
  if(!edit.valid) return false;
  ui.editor.replaceRange(edit.start, edit.end, edit.text);
  if(edit.selects) ui.editor.selectRange(edit.anchor, edit.cursor);
  else ui.editor.moveCursor(edit.cursor);
  ui.markEdited();
  ui.revealEditorCursor = true;
  return true;
}

void wrapEditorSelection(UiRuntime& ui, std::string_view open, std::string_view close, std::string_view label) {
  if(ui.focus != FocusArea::Editor) return;
  const std::size_t start = ui.editor.hasSelection() ? ui.editor.selectionStart() : ui.editor.cursor();
  const std::size_t end = ui.editor.hasSelection() ? ui.editor.selectionEnd() : ui.editor.cursor();
  if(applyEdit(ui, doc::wrapSelection(ui.editor.text(), start, end, open, close))) ui.status = std::string(label);
}

void linkEditorSelection(UiRuntime& ui) {
  if(ui.focus != FocusArea::Editor) return;
  const std::size_t start = ui.editor.hasSelection() ? ui.editor.selectionStart() : ui.editor.cursor();
  const std::size_t end = ui.editor.hasSelection() ? ui.editor.selectionEnd() : ui.editor.cursor();
  if(applyEdit(ui, doc::makeLink(ui.editor.text(), start, end))) ui.status = "Link: type the destination";
}

// The blocks a command applies to: the block selection when there is one,
// otherwise the block holding the caret. Both ends are carets, not indices.
std::pair<std::size_t, std::size_t> blockSelectionCarets(const UiRuntime& ui) {
  if(!ui.blockSelectActive) return {ui.editor.cursor(), ui.editor.cursor()};
  return {std::min(ui.blockSelectAnchor, ui.blockSelectFocus),
          std::max(ui.blockSelectAnchor, ui.blockSelectFocus)};
}

void selectBlockAtCursor(UiRuntime& ui) {
  const EditorBlocks blocks(ui);
  std::size_t index = doc::blockIndexAt(blocks, ui.editor.cursor());
  // A blank line - the empty last line included - is a separator, not something
  // to select: step back to the nearest real block.
  while(index > 0 && blocks[index].kind == doc::BlockKind::Blank) --index;
  ui.blockSelectActive = true;
  ui.blockSelectAnchor = blocks[index].start;
  ui.blockSelectFocus = blocks[index].start;
  ui.editor.moveCursor(blocks[index].start);
  ui.editor.clearSelection();
  ui.editor.breakUndoGroup();
}

// A range transform hands back its result as a text selection. A block
// selection wants the same span expressed as blocks again.
void syncBlockSelectionToEdit(UiRuntime& ui) {
  if(!ui.blockSelectActive) return;
  if(ui.editor.hasSelection()) {
    ui.blockSelectAnchor = ui.editor.selectionStart();
    // One byte inside the last block, not the boundary after it, so the range
    // does not reach into whatever follows.
    ui.blockSelectFocus = ui.editor.selectionEnd() > ui.blockSelectAnchor ? ui.editor.selectionEnd() - 1
                                                                         : ui.blockSelectAnchor;
    ui.editor.clearSelection();
    ui.editor.moveCursor(ui.blockSelectAnchor);
  } else {
    ui.blockSelectAnchor = ui.editor.cursor();
    ui.blockSelectFocus = ui.editor.cursor();
  }
}

// Moves the focus end of a block selection by whole blocks. Blanks are skipped:
// they are separators, not something a user means to select.
void moveBlockSelection(UiRuntime& ui, int delta, bool extend) {
  const EditorBlocks blocks(ui);
  std::size_t next = doc::blockIndexAt(blocks, ui.blockSelectFocus);
  bool moved = false;
  while(true) {
    if(delta < 0) {
      if(next == 0) break;
      --next;
    } else {
      if(next + 1 >= blocks.size()) break;
      ++next;
    }
    if(blocks[next].kind != doc::BlockKind::Blank) {
      moved = true;
      break;
    }
  }
  if(!moved) return;
  ui.blockSelectFocus = blocks[next].start;
  if(!extend) ui.blockSelectAnchor = ui.blockSelectFocus;
  ui.editor.moveCursor(blocks[next].start);
  ui.editor.clearSelection();
  ui.revealEditorCursor = true;
}

void turnCurrentBlockInto(UiRuntime& ui, doc::BlockKind kind, int level, std::string_view label) {
  if(ui.focus != FocusArea::Editor) return;
  const auto [from, to] = blockSelectionCarets(ui);
  if(applyEdit(ui, doc::turnBlocksInto(ui.editor.text(), from, to, kind, level, editorBlocks(ui)))) {
    syncBlockSelectionToEdit(ui);
    ui.status = std::string(label);
  } else {
    ui.status = "Already " + std::string(label);
  }
}

constexpr BlockKindEntry kBlockKinds[] = {
  {"turn:text", "Text", "Plain paragraph", doc::BlockKind::Paragraph, 0},
  {"turn:h1", "Heading 1", "# ", doc::BlockKind::Heading, 1},
  {"turn:h2", "Heading 2", "## ", doc::BlockKind::Heading, 2},
  {"turn:h3", "Heading 3", "### ", doc::BlockKind::Heading, 3},
  {"turn:bullet", "Bulleted list", "- ", doc::BlockKind::Bullet, 0},
  {"turn:ordered", "Numbered list", "1. ", doc::BlockKind::Ordered, 0},
  {"turn:todo", "To-do list", "- [ ] ", doc::BlockKind::Todo, 0},
  {"turn:quote", "Quote", "> ", doc::BlockKind::Quote, 0},
  {"turn:callout", "Callout", "> [!NOTE] ", doc::BlockKind::Callout, 0},
  {"turn:tip", "Tip callout", "> [!TIP] ", doc::BlockKind::Callout, 1},
  {"turn:important", "Important callout", "> [!IMPORTANT] ", doc::BlockKind::Callout, 2},
  {"turn:warning", "Warning callout", "> [!WARNING] ", doc::BlockKind::Callout, 3},
  {"turn:caution", "Caution callout", "> [!CAUTION] ", doc::BlockKind::Callout, 4},
  {"turn:code", "Code block", "```", doc::BlockKind::Code, 0},
  {"turn:divider", "Divider", "---", doc::BlockKind::Divider, 0},
};

const BlockKindEntry* blockKindFor(std::string_view id) {
  for(const auto& entry : kBlockKinds) {
    if(id == entry.id) return &entry;
  }
  return nullptr;
}

bool moveSelectedBlocks(UiRuntime& ui, int delta) {
  const auto [from, to] = blockSelectionCarets(ui);
  if(!applyEdit(ui, doc::moveBlocks(ui.editor.text(), from, to, delta, editorBlocks(ui)))) return false;
  syncBlockSelectionToEdit(ui);
  ui.status = delta < 0 ? "Moved block up" : "Moved block down";
  return true;
}

// Folding changes what is on screen and never the file, so it goes nowhere near
// the editor or the undo stack.
void toggleFoldAt(UiRuntime& ui, std::size_t caret) {
  const std::string& source = ui.editor.text();
  const EditorBlocks blocks(ui);
  const std::size_t index = doc::foldHeadFor(blocks, doc::blockIndexAt(blocks, std::min(caret, source.size())));
  if(index >= blocks.size()) {
    ui.status = "Nothing to fold here";
    return;
  }
  const bool folded = ui.folds.toggle(ui.state.selection().noteId, doc::foldKey(source, blocks[index]));
  // A section that just collapsed must not be left holding the caret. Only the
  // blocks it actually hides count: the caret further down the note stays put.
  const std::size_t end = doc::foldEnd(blocks, index);
  const std::size_t caretNow = ui.editor.cursor();
  if(folded && caretNow >= blocks[index].end() && caretNow < blocks[end - 1].end()) {
    ui.editor.moveCursor(blocks[index].contentEnd());
    ui.editor.clearSelection();
  }
  ui.status = folded ? "Folded" : "Unfolded";
  ui.revealEditorCursor = true;
}

// The one place block commands are dispatched, shared by the block menu, the
// slash menu, the selection toolbar and the keyboard.
void performBlockCommand(UiRuntime& ui, const std::string& id) {
  if(const auto* entry = blockKindFor(id)) {
    turnCurrentBlockInto(ui, entry->kind, entry->level, entry->label);
    return;
  }
  const auto [from, to] = blockSelectionCarets(ui);
  if(id == "duplicate") {
    if(applyEdit(ui, doc::duplicateBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
      syncBlockSelectionToEdit(ui);
      ui.status = "Duplicated block";
    }
  } else if(id == "delete") {
    if(applyEdit(ui, doc::deleteBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
      syncBlockSelectionToEdit(ui);
      ui.status = "Deleted block";
    }
  } else if(id == "move-up") {
    moveSelectedBlocks(ui, -1);
  } else if(id == "move-down") {
    moveSelectedBlocks(ui, 1);
  } else if(id == "fold") {
    toggleFoldAt(ui, ui.editor.cursor());
  }
}


std::span<const BlockKindEntry> blockKinds() {
  return kBlockKinds;
}

bool undoEditorEdit(UiRuntime& ui) {
  if(!ui.editor.undo()) return false;
  ui.markEdited();
  ui.revealEditorCursor = true;
  ui.status = "Undo";
  return true;
}

bool redoEditorEdit(UiRuntime& ui) {
  if(!ui.editor.redo()) return false;
  ui.markEdited();
  ui.revealEditorCursor = true;
  ui.status = "Redo";
  return true;
}

}
