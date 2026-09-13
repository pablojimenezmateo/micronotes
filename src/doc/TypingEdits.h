#pragma once

#include "doc/Edits.h"

namespace micronotes::doc {

// What a *key* means in Markdown, as against what a *command* does to a block.
//
// These four are the reflexes: Enter continues the list you are in and closes
// the fence you just opened, Backspace at the first content byte peels a marker
// off, and a typed space finishes a task checkbox. Every one of them is decided
// by where the caret is rather than by anything the reader chose from a menu,
// and every one of them runs on a keystroke -- which is why they take the same
// borrowed `BlockSpan` the block commands do, and why the shortcut rejects on
// two bytes before it will pay for a partition.
//
// They were the last hundred lines of `doc/Edits.cpp`, which made that file two
// subjects and the largest source in the tree. The tell is the caller: the
// block commands are reached from `app/Commands.cpp`, `app/BlockMenus.cpp` and
// `app/PagePress.cpp`, and these four from `app/KeyRouter.cpp` and
// `app/KeySurfaces.cpp` and nowhere else.
//
// The `Edit` they return is the same one, applied the same way -- one
// erase-and-insert onto the single undo stack.

// Enter inside a list, quote or callout: continues the block with a fresh
// marker, or strips the marker when the item is empty. Invalid elsewhere, which
// means the caller should insert a plain newline.
Edit continueList(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// Enter on the opening line of an unterminated fence: adds the closing fence
// and leaves the caret on the blank line between them.
Edit closeFence(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// Backspace at the first content byte of a block: outdents a nested list item,
// otherwise removes the block's marker.
Edit outdentOrUnwrap(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// Run after a character is typed. Only shapes Markdown cannot already express
// are rewritten: "[] " and "[x] " become proper task markers. Everything
// else ("# ", "- ", "1. ", "> ", "---", "```") is already the real syntax and
// is left exactly as typed.
Edit applyMarkdownShortcut(std::string_view source, std::size_t caret, BlockSpan blocks = {});

}
