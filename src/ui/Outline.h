#pragma once

#include "doc/BlockScan.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// One heading in the open note, with where to put the caret to get to it.
struct OutlineEntry {
  int level = 1;          // 1-6, as written
  // How far to indent the row. Derived from the headings actually present, not
  // from `level`: a note whose every heading is an h3 should read as a flat
  // list, not as a list pushed three steps to the right.
  int depth = 0;
  std::string text;       // the heading, with its marker and trailing #s removed
  std::size_t offset = 0; // where the heading's own text starts, for the caret
};

// The headings of a note, in the order they appear, into a vector the caller
// owns and over a block partition it already has.
//
// Both halves of that matter, because this runs on **every keystroke** while the
// outline panel is open -- it is keyed on the editor's revision, and that moves
// with every typed character.
//
// The partition is the expensive half. Deriving one is a pass over every byte of
// the note plus a fresh `vector<SourceBlock>`, which on a 200 KB note is 194 us
// of the 226 us this cost -- against the ~14 us that keystroke's own layout
// update costs. The live surface has the partition already, spliced rather than
// rescanned, and `app::EditorBlocks` is what lends it; this is the same borrow
// the block edits in `doc/Edits.h` take, for the same reason.
//
// An empty `blocks` means "I have none", and this scans -- which is what a
// caller with no layout behind it, a test for instance, should get.
void outlineInto(std::string_view source, doc::BlockSpan blocks,
                 std::vector<OutlineEntry>* out);

// The same, allocating its own vector and its own partition.
//
// A pure function of the source, so the outline panel is testable without a
// window: the interesting cases are the ones that only look like headings --
// a `#` inside a fenced code block, or partway through a paragraph.
std::vector<OutlineEntry> outlineOf(std::string_view source);

// The entry the caret is inside, or npos when the caret is above the first
// heading. Used to mark the reader's place in the panel.
std::size_t outlineEntryAt(const std::vector<OutlineEntry>& entries, std::size_t caret);

}
