#pragma once

#include <cstddef>
#include <string>

// The four menus that act on a block, and the two commits that close them.
//
// A block is the unit the live surface edits, and there are four ways to ask
// something of one: the gutter's insert button, a typed "/", a right click, and
// "turn into" from either the toolbar or the palette. All four are the same
// list of block types -- `blockKinds()` in `app/EditCommands.h` -- and all four
// were `static` in Application.cpp, which is why the two that *write* had to be
// there too.
//
// The pairing is the point. `openSlashMenu` records where the "/" was typed and
// `commitSlashMenu` erases back to it; `openInsertMenu` sets the same fields to
// mean "insert after this block instead". Those are three fields with one
// contract between them (`ui.slash.start`, `slashInserts`, `slashAfterBlock`),
// and the open and the commit are the only two functions allowed to know it.
namespace micronotes::app {

struct UiRuntime;

// Anchored at the pointer: turn the block under the caret into another kind.
void openTurnIntoMenu(UiRuntime& ui, float x, float y);
// The right-click menu on a block: turn into, duplicate, fold, move, delete.
void openBlockMenu(UiRuntime& ui, float x, float y);
// `slashStart` is the "/" the user typed; committing erases [slashStart, caret)
// before the block transform runs.
void openSlashMenu(UiRuntime& ui, std::size_t slashStart);
// Opened from the gutter's insert button: nothing is written until a block type
// is chosen, so dismissing the menu leaves the note exactly as it was.
void openInsertMenu(UiRuntime& ui, std::size_t blockStart);
void commitSlashMenu(UiRuntime& ui, const std::string& itemId);

// Appends the selected blocks to another note and removes them from this one.
// The source edit goes through the undo stack as any block edit does; the
// target is not open, so it is written directly.
void moveBlocksToNote(UiRuntime& ui, const std::string& targetId);

}
