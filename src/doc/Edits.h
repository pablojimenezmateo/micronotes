#pragma once

#include "doc/BlockScan.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace micronotes::doc {

// Every block operation is one erase-and-insert against the note buffer, so it
// flows through `MarkdownEditor::replaceRange` and lands on the single undo
// stack. Nothing here keeps parallel state or reformats bytes the user did not
// ask to change.
//
// Every operation here also needs the buffer's block partition, to find the one
// block it acts on. Each takes a trailing `BlockSpan`: pass the partition you
// already have, and it is borrowed; pass nothing and the operation scans the
// buffer for itself.
//
// The difference is not small. A full scan is a pass over every byte plus a
// fresh `vector<SourceBlock>` -- 88 bytes a block, ~10,000 blocks on a 200 KB
// note -- and it measured ~200 us against the 40 us that note's whole layout
// update costs. Enter ran two of them (`closeFence` then `continueList`), three
// if it landed on an empty nested list item. The live page has the exact same
// partition sitting in `DocumentLayout`, spliced incrementally on every
// keystroke, so the scan was re-deriving what was already in hand.
//
// A borrowed span must partition `source`. `DocumentLayout::blocksAt` is how a
// caller establishes that without a comparison: it answers only for the
// revision the layout was built from.
struct Edit {
  bool valid = false;
  std::size_t start = 0;   // erase [start, end) ...
  std::size_t end = 0;
  std::string text;        // ... and insert this at `start`
  std::size_t cursor = 0;  // caret afterwards, as an offset into the new buffer
  std::size_t anchor = 0;  // selection anchor afterwards
  bool selects = false;    // whether the caller should restore a selection
};

// Rewrites the block's marker, keeping its content. `level` applies to
// headings. Code and Divider replace the whole block. Complex blocks are
// refused: the scanner does not model them, so it must not rewrite them.
Edit turnInto(std::string_view source, std::size_t caret, BlockKind kind, int level = 1,
              BlockSpan blocks = {});

// Flips a task's checkbox. A bullet or paragraph becomes an unchecked task.
Edit toggleTodo(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// List nesting, two columns at a time. `indent` refuses the first item of a
// list level (Markdown has nothing for it to nest under) and `outdent` refuses
// an item already at the left margin.
Edit indent(std::string_view source, std::size_t caret, BlockSpan blocks = {});
Edit outdent(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// Swaps the block, or the blocks two carets span, with the nearest non-blank
// neighbour, carrying the blank line that separated them along so the two do
// not merge. `delta` is -1 for up, +1 for down.
Edit moveBlock(std::string_view source, std::size_t caret, int delta, BlockSpan blocks = {});
Edit moveBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret, int delta,
                BlockSpan blocks = {});
Edit duplicateBlock(std::string_view source, std::size_t caret, BlockSpan blocks = {});
Edit deleteBlock(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// The same three operations over every block two carets touch, for block
// multi-select. A range of one block behaves exactly like the singles above,
// except that the result selects what it produced so the selection follows.
Edit deleteBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                  BlockSpan blocks = {});
Edit duplicateBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                     BlockSpan blocks = {});
Edit turnBlocksInto(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                    BlockKind kind, int level = 1, BlockSpan blocks = {});

// Adds a new empty block of `kind` after the block holding `caret`, past the
// blank line that separates it from the next one so the new block lands between
// the two rather than inside the separator. Backs the gutter's insert button.
Edit insertBlockAfter(std::string_view source, std::size_t caret, BlockKind kind, int level = 1,
                      BlockSpan blocks = {});

// Drag-to-reorder: lifts the blocks two carets touch and drops them at
// `destination`, a block boundary offset. Still one erase-and-insert - the
// erased span simply stretches from the blocks to the drop point. Refused when
// the destination is inside the blocks being moved, or already where they are.
Edit moveBlocksTo(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                  std::size_t destination, BlockSpan blocks = {});

// Wraps [start, end) in `open`/`close`, or unwraps when the markers are already
// exactly there. With an empty range it inserts the pair and sits between them.
Edit wrapSelection(std::string_view source, std::size_t start, std::size_t end,
                   std::string_view open, std::string_view close);

// Turns [start, end) into `[text](target)`, leaving the caret inside the
// parentheses when `target` is empty.
Edit makeLink(std::string_view source, std::size_t start, std::size_t end, std::string_view target = "");

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
// live are rewritten: "[] " and "[x] " become proper task markers. Everything
// else ("# ", "- ", "1. ", "> ", "---", "```") is already the real syntax and
// is left exactly as typed.
Edit applyMarkdownShortcut(std::string_view source, std::size_t caret, BlockSpan blocks = {});

// The marker text a block of this shape is written with, indentation included.
std::string blockMarker(BlockKind kind, int level, int listDepth, int ordinal, bool checked);

}
