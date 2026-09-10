#include "app/EditCommands.h"

#include "app/Notes.h"

#include <algorithm>
#include <cctype>

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

// The blocks a command applies to: the block selection when there is one, the
// blocks a *text* selection covers when there is one of those, and otherwise
// the block holding the caret. Both ends are carets, not indices.
//
// The middle case is the one that was missing, and every block command shared
// the omission: with three lines selected by dragging or by Shift+Down, this
// answered with the bare caret, so Alt+Up moved the one block the caret
// happened to be in and left the rest of the selection where it was. There are
// two ways to select several blocks -- Escape into the block selection, or just
// drag across them -- and only one of them was being heard.
std::pair<std::size_t, std::size_t> blockSelectionCarets(const UiRuntime& ui) {
  if(ui.blockSelection.active) {
    return {std::min(ui.blockSelection.anchor, ui.blockSelection.focus),
            std::max(ui.blockSelection.anchor, ui.blockSelection.focus)};
  }
  if(ui.editor.hasSelection()) {
    const std::size_t start = ui.editor.selectionStart();
    const std::size_t end = ui.editor.selectionEnd();
    // One byte inside the last block selected, not the boundary after it: a
    // selection that stops exactly at a block's end -- which is what
    // Shift+Down onto the next line gives -- would otherwise reach into the
    // block that follows and take it along. The same convention
    // `syncBlockSelectionToEdit` uses, and for the same reason.
    return {start, end > start ? end - 1 : start};
  }
  return {ui.editor.cursor(), ui.editor.cursor()};
}

void selectBlockAtCursor(UiRuntime& ui) {
  const EditorBlocks blocks(ui);
  std::size_t index = doc::blockIndexAt(blocks, ui.editor.cursor());
  // A blank line - the empty last line included - is a separator, not something
  // to select: step back to the nearest real block.
  while(index > 0 && blocks[index].kind == doc::BlockKind::Blank) --index;
  ui.blockSelection.active = true;
  ui.blockSelection.anchor = blocks[index].start;
  ui.blockSelection.focus = blocks[index].start;
  ui.editor.moveCursor(blocks[index].start);
  ui.editor.clearSelection();
  ui.editor.breakUndoGroup();
}

// A range transform hands back its result as a text selection. A block
// selection wants the same span expressed as blocks again.
void syncBlockSelectionToEdit(UiRuntime& ui) {
  if(!ui.blockSelection.active) return;
  if(ui.editor.hasSelection()) {
    ui.blockSelection.anchor = ui.editor.selectionStart();
    // One byte inside the last block, not the boundary after it, so the range
    // does not reach into whatever follows.
    ui.blockSelection.focus = ui.editor.selectionEnd() > ui.blockSelection.anchor ? ui.editor.selectionEnd() - 1
                                                                         : ui.blockSelection.anchor;
    ui.editor.clearSelection();
    ui.editor.moveCursor(ui.blockSelection.anchor);
  } else {
    ui.blockSelection.anchor = ui.editor.cursor();
    ui.blockSelection.focus = ui.editor.cursor();
  }
}

// Moves the focus end of a block selection by whole blocks. Blanks are skipped:
// they are separators, not something a user means to select.
void moveBlockSelection(UiRuntime& ui, int delta, bool extend) {
  const EditorBlocks blocks(ui);
  std::size_t next = doc::blockIndexAt(blocks, ui.blockSelection.focus);
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
  ui.blockSelection.focus = blocks[next].start;
  if(!extend) ui.blockSelection.anchor = ui.blockSelection.focus;
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
  {"turn:text", "Text", "Plain paragraph", doc::BlockKind::Paragraph, 0, '0'},
  {"turn:h1", "Heading 1", "# ", doc::BlockKind::Heading, 1, '1'},
  {"turn:h2", "Heading 2", "## ", doc::BlockKind::Heading, 2, '2'},
  {"turn:h3", "Heading 3", "### ", doc::BlockKind::Heading, 3, '3'},
  {"turn:bullet", "Bulleted list", "- ", doc::BlockKind::Bullet, 0, '8'},
  {"turn:ordered", "Numbered list", "1. ", doc::BlockKind::Ordered, 0, '7'},
  {"turn:todo", "To-do list", "- [ ] ", doc::BlockKind::Todo, 0, '9'},
  {"turn:quote", "Quote", "> ", doc::BlockKind::Quote, 0},
  {"turn:callout", "Callout", "> [!NOTE] ", doc::BlockKind::Callout, 0},
  {"turn:tip", "Tip callout", "> [!TIP] ", doc::BlockKind::Callout, 1},
  {"turn:important", "Important callout", "> [!IMPORTANT] ", doc::BlockKind::Callout, 2},
  {"turn:warning", "Warning callout", "> [!WARNING] ", doc::BlockKind::Callout, 3},
  {"turn:caution", "Caution callout", "> [!CAUTION] ", doc::BlockKind::Callout, 4},
  {"turn:code", "Code block", "```", doc::BlockKind::Code, 0},
  {"turn:divider", "Divider", "---", doc::BlockKind::Divider, 0},
};

const BlockKindEntry* blockKindForChordDigit(char digit) {
  if(digit == 0) return nullptr;
  for(const auto& entry : kBlockKinds) {
    if(entry.chordDigit == digit) return &entry;
  }
  return nullptr;
}

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


void selectWordAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  std::size_t cursor = std::min(ui.editor.cursor(), value.size());
  if(cursor > 0 && (cursor == value.size() || !std::isalnum(static_cast<unsigned char>(value[cursor])))) --cursor;
  std::size_t start = cursor;
  std::size_t end = cursor;
  while(start > 0 && (std::isalnum(static_cast<unsigned char>(value[start - 1])) || value[start - 1] == '_')) --start;
  while(end < value.size() && (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_')) ++end;
  ui.editor.selectRange(start, end);
}

void selectLineAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  const auto cursor = std::min(ui.editor.cursor(), value.size());
  const auto lineStart = value.rfind('\n', cursor == 0 ? 0 : cursor - 1);
  const auto lineEnd = value.find('\n', cursor);
  const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
  const std::size_t end = lineEnd == std::string::npos ? value.size() : lineEnd;
  ui.editor.selectRange(start, end);
}

}
