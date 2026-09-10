#pragma once

#include <cstddef>
#include <string>

// The two menus that act on a block, and the commit that closes one of them.
//
// There are two ways to ask something of a block: a typed "/", and "turn into"
// from the palette or the menu bar. Both are the same list of block types --
// `blockKinds()` in `app/EditCommands.h` -- and both were `static` in
// Application.cpp, which is why the one that *writes* had to be there too.
//
// The pairing is the point: `openSlashMenu` records where the "/" was typed and
// `commitSlashMenu` erases back to it. That is one field with one contract on
// it (`ui.slash.start`), and the open and the commit are the only two
// functions allowed to know it.
namespace micronotes::app {

struct UiRuntime;

// Anchored at the pointer: turn the block under the caret into another kind.
void openTurnIntoMenu(UiRuntime& ui, float x, float y);
// `slashStart` is the "/" the user typed; committing erases [slashStart, caret)
// before the block transform runs.
void openSlashMenu(UiRuntime& ui, std::size_t slashStart);
void commitSlashMenu(UiRuntime& ui, const std::string& itemId);

// Appends the blocks the caret or the selection covers to another note and
// removes them from this one. The source edit goes through the undo stack as
// any block edit does; the target is not open, so it is written directly.
void moveBlocksToNote(UiRuntime& ui, const std::string& targetId);

}
