#pragma once

#include "app/Shell.h"
#include "doc/BlockScan.h"

#include <cstdint>
#include <vector>

namespace micronotes::app {

// Where a block edit gets the buffer's block partition.
//
// Every operation in `doc/Edits.h` needs the partition to find the one block it
// acts on, and each used to derive it by scanning the whole note: a pass over
// every byte plus a fresh `vector<SourceBlock>` at 88 bytes a block, which on a
// 200 KB note measured ~200 us against the 40 us the keystroke's own layout
// update costs. Enter paid it twice (`closeFence`, then `continueList`) and
// three times when it landed on an empty nested list item. Backspace, Tab and
// every block command paid it once.
//
// The live surface has the same partition already, spliced rather than
// rescanned, from the frame it just drew. These two hand it over.
//
// The editor's revision is the entire safety argument, and it is the reason this
// is not simply "read `document().blocks()`". The layout's blocks describe the
// buffer the layout was last given; the editor's buffer may have moved since.
// `blocksAt` answers only for the revision it was built from, so the borrow is
// either exactly this buffer's partition or nothing at all. Nothing is a
// perfectly good answer -- the caller scans, as it always did -- and it is what
// comes back for the raw pane, for the first keystroke after a note opens, and
// for the second of two edits inside one key handler.

// The partition to lend a `doc::Edits` call, or an empty span when the page has
// none for this revision of the buffer.
doc::BlockSpan editorBlocks(const UiRuntime& ui);

// The same partition as something a local can hold, for the call sites that
// index it directly instead of handing it to a transform. Borrowed when the page
// has one, scanned once here when it does not, so a caller writes `blocks[i]`
// without knowing which it got.
class EditorBlocks {
public:
  explicit EditorBlocks(const UiRuntime& ui);

  operator doc::BlockSpan() const {
    return blocks();
  }

  const doc::SourceBlock& operator[](std::size_t index) const {
    return blocks()[index];
  }

  std::size_t size() const {
    return blocks().size();
  }

private:
  // Derived, not stored: a member span into `owned_` would dangle if this were
  // ever copied, and nothing about the class would say so at the point of the
  // copy. One branch per access instead.
  doc::BlockSpan blocks() const {
    return lent_.empty() ? doc::BlockSpan(owned_) : lent_;
  }

  std::vector<doc::SourceBlock> owned_;
  doc::BlockSpan lent_;
};

}
