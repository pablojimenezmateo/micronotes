#pragma once

#include "doc/BlockScan.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace micronotes::doc {

// The block partition an edit reads, and the two spans every edit works in.
//
// Shared by `doc/Edits.h`'s block commands and `doc/TypingEdits.h`'s keystroke
// reflexes, which is why it is a unit rather than a block of statics: it was
// one, at the top of a 844-line `Edits.cpp` that held both, and splitting that
// file was going to mean either copying this or exporting it. The copy is the
// worse of the two -- the synthetic last line below is exactly the kind of rule
// that gets fixed in one copy.
//
// Internal to `doc/`. Nothing above this layer names `Context` or `Range`; what
// they hand back is an `Edit`.

// The partition an edit reads, from whichever of the two places it came.
//
// A caller that already holds one lends it and nothing is scanned; a caller that
// does not gets a scan into this object's own vector. Either way the reader sees
// one indexable sequence, which is why `Context` and `Range` below did not have
// to learn about the difference.
//
// The one thing it adds to both is the empty last line. A buffer ending in a
// newline leaves a caret position no block covers, and it needs one of its own
// so Enter there starts a paragraph instead of continuing the list above it, and
// so the block commands find nothing there to act on. That used to be a real
// entry pushed onto the scan's vector, which a borrowed span cannot have -- so
// it is synthesised here, past the end of whatever was handed over.
class BlockList {
public:
  BlockList() = default;

  void reset(std::string_view source, BlockSpan lent);

  std::size_t size() const {
    return real().size() + (lastLine_ ? 1 : 0);
  }

  const SourceBlock& operator[](std::size_t index) const {
    const BlockSpan blocks = real();
    return index < blocks.size() ? blocks[index] : trailing_;
  }

  // The block owning `offset`, over the synthetic entry as well as the real
  // ones: `blockIndexAt` knows only about the span it is given, and the empty
  // last line starts exactly where the last real block ends.
  std::size_t indexAt(std::string_view source, std::size_t offset) const;

private:
  // Derived rather than stored. A member span pointing into `owned_` dangles the
  // moment the object is copied or moved -- and `Context` and `Range` are both
  // returned by value, so whether that happens is a question about copy elision
  // rather than about this class. One branch per access removes the question.
  BlockSpan real() const {
    return lent_.empty() ? BlockSpan(owned_) : lent_;
  }

  std::vector<SourceBlock> owned_;
  BlockSpan lent_;
  SourceBlock trailing_;
  bool lastLine_ = false;
};

// One block, found from a caret.
struct Context {
  BlockList blocks;
  std::size_t index = 0;
};

Context contextAt(std::string_view source, std::size_t caret, BlockSpan lent);

// The span of whole blocks two carets reach across.
//
// The empty last line is in the partition here as it is in `contextAt` -- it is
// somewhere a block can be *started*, which is what the slash menu does there --
// but it spans no bytes, so a range that lands on it comes back `valid = false`
// and every operation over a range refuses it. (The comment that used to sit
// here said this list "never invents a trailing block", which was never what the
// code did: it called the same scan as `contextAt`. The behaviour it was
// describing is the `end > start` test below.)
struct Range {
  bool valid = false;
  BlockList blocks;
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t start = 0;
  std::size_t end = 0;
};

Range rangeAt(std::string_view source, std::size_t from, std::size_t to, BlockSpan lent);

// The blank run that separates [first, last] from its neighbour: the one after
// the group when there is one, otherwise the one before it. Every block command
// carries this run along, because dropping two paragraphs against each other
// with no blank line between them merges them into one.
struct Separator {
  std::size_t start = 0;
  std::size_t end = 0;
  bool leading = false;  // the run sits before the group rather than after it
};

Separator separatorFor(const BlockList& blocks, std::size_t first, std::size_t last);

std::size_t leadingWhitespace(std::string_view source, const SourceBlock& block);
std::size_t lineEndFrom(std::string_view source, std::size_t start);

// Keeps a caret that sat inside the block's content pointing at the same
// character after the marker in front of it changed length.
std::size_t shiftedCaret(std::size_t caret, std::size_t contentStart, std::size_t newContentStart);

}
