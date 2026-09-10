#pragma once

#include "app/EditorBlocks.h"
#include "app/Shell.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <utility>

namespace micronotes::app {

// Every verb that changes the open note's text, in one place.
//
// These lived at the top of `Application.cpp` as file-static functions, which
// meant the only thing that could reach them was the key handler in the same
// file -- so the palette and the menu bar could *offer* Bold and Undo and
// nothing would happen when either was chosen. `ArchitectureTests` caught it
// the moment the menu bar listed them.
//
// They were never palette commands for a good reason: the palette takes the
// keyboard away from the editor, so a verb acting on the selection has nothing
// left to act on by the time it runs. A menu does not -- `ui.focus` stays where
// it was while a menu is open -- so the same verbs are real menu items, and
// that is what pulled them out here.

// One erase-and-insert against the buffer as it stands, landing on the editor's
// single undo stack instead of keeping state of its own.
bool applyEdit(UiRuntime& ui, const doc::Edit& edit);

// Runs a transform against the buffer as it stands right now. Chaining these
// with `||` is safe: a transform only sees the buffer when the earlier ones
// declined to change it -- and a transform that did change it has moved the
// editor's revision on, so the next one in the chain is handed no partition
// rather than a stale one.
template <typename Transform>
bool applyTransform(UiRuntime& ui, Transform&& transform) {
  return applyEdit(ui, transform(ui.editor.text(), ui.editor.cursor(), editorBlocks(ui)));
}

// Inline marks. `label` is what the status bar says afterwards.
void wrapEditorSelection(UiRuntime& ui, std::string_view open, std::string_view close,
                         std::string_view label);
void linkEditorSelection(UiRuntime& ui);

// Undo and redo of the note's text, wherever the request came from. The field
// editors have their own stacks and are handled by the key path that owns them.
bool undoEditorEdit(UiRuntime& ui);
bool redoEditorEdit(UiRuntime& ui);

// The blocks a command applies to: the blocks a text selection covers when
// there is one, otherwise the block holding the caret. Both ends are carets,
// not indices.
std::pair<std::size_t, std::size_t> blockCommandRange(const UiRuntime& ui);

// The word or the line the caret sits in, as the editor's selection.
//
// Here rather than with the key handler because both routers want them: a
// double click selects a word and Ctrl+Shift+Left extends by one, and the two
// were a `static` the pointer path could only reach because it happened to sit
// in the same file.
void selectWordAtCursor(UiRuntime& ui);
void selectLineAtCursor(UiRuntime& ui);

void turnCurrentBlockInto(UiRuntime& ui, doc::BlockKind kind, int level, std::string_view label);

// Every block shape the slash menu and the turn-into submenu can produce. One
// table, so the two stay in step.
struct BlockKindEntry {
  const char* id;
  const char* label;
  const char* detail;
  doc::BlockKind kind;
  int level;
  // The digit that turns a block into this one with Ctrl+Shift, or 0 for a
  // shape the keyboard cannot reach directly.
  //
  // Here rather than in the key router because the router had all of it
  // written out a second time -- the kind, the level *and* the label -- and the
  // labels had drifted: the keyboard said "heading 1" and "a bullet" where this
  // table says "Heading 1" and "Bulleted list", so the status message depended
  // on which way you asked. The shortcut list had drifted further, advertising
  // "Ctrl+Shift+1-9" when 4, 5 and 6 reached no branch at all.
  char chordDigit = 0;
};

std::span<const BlockKindEntry> blockKinds();
const BlockKindEntry* blockKindFor(std::string_view id);
// The block shape Ctrl+Shift+<digit> produces, or null when that digit is not
// bound. One lookup, so the keyboard cannot offer a shape the table does not.
const BlockKindEntry* blockKindForChordDigit(char digit);

bool moveSelectedBlocks(UiRuntime& ui, int delta);

// The one place block commands are dispatched, shared by the slash menu, the
// turn-into menu, the palette, the menu bar and the keyboard.
void performBlockCommand(UiRuntime& ui, const std::string& id);

}
