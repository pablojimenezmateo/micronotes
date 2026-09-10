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

// The blocks a command applies to: the blocks a text selection covers when
// there is one, and otherwise the block holding the caret. Both ends are
// carets, not indices.
//
// The selection case is the one that was missing, and every block command
// shared the omission: with three lines selected by dragging or by Shift+Down,
// this answered with the bare caret, so Alt+Up moved the one block the caret
// happened to be in and left the rest of the selection where it was.
std::pair<std::size_t, std::size_t> blockCommandRange(const UiRuntime& ui) {
  if(ui.editor.hasSelection()) {
    const std::size_t start = ui.editor.selectionStart();
    const std::size_t end = ui.editor.selectionEnd();
    // One byte inside the last block selected, not the boundary after it: a
    // selection that stops exactly at a block's end -- which is what
    // Shift+Down onto the next line gives -- would otherwise reach into the
    // block that follows and take it along.
    return {start, end > start ? end - 1 : start};
  }
  return {ui.editor.cursor(), ui.editor.cursor()};
}

void turnCurrentBlockInto(UiRuntime& ui, doc::BlockKind kind, int level, std::string_view label) {
  if(ui.focus != FocusArea::Editor) return;
  const auto [from, to] = blockCommandRange(ui);
  if(applyEdit(ui, doc::turnBlocksInto(ui.editor.text(), from, to, kind, level, editorBlocks(ui)))) {
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
  const auto [from, to] = blockCommandRange(ui);
  if(!applyEdit(ui, doc::moveBlocks(ui.editor.text(), from, to, delta, editorBlocks(ui)))) return false;
  ui.status = delta < 0 ? "Moved block up" : "Moved block down";
  return true;
}

// The one place block commands are dispatched, shared by the slash menu, the
// palette and the keyboard.
void performBlockCommand(UiRuntime& ui, const std::string& id) {
  if(const auto* entry = blockKindFor(id)) {
    turnCurrentBlockInto(ui, entry->kind, entry->level, entry->label);
    return;
  }
  const auto [from, to] = blockCommandRange(ui);
  if(id == "duplicate") {
    if(applyEdit(ui, doc::duplicateBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
      ui.status = "Duplicated block";
    }
  } else if(id == "delete") {
    if(applyEdit(ui, doc::deleteBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
      ui.status = "Deleted block";
    }
  } else if(id == "move-up") {
    moveSelectedBlocks(ui, -1);
  } else if(id == "move-down") {
    moveSelectedBlocks(ui, 1);
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
