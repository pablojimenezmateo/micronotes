#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::doc {

// The block types the live editing surface models directly. Anything else is
// tagged `Complex` and handed to the md4c render model wholesale, so the
// scanner never has to guess at constructs it cannot round-trip.
enum class BlockKind {
  Blank,
  Paragraph,
  Heading,
  Bullet,
  Ordered,
  Todo,
  Quote,
  Callout,
  Code,
  Divider,
  Complex
};

// A block's exact source range. `start`/`end` partition the buffer; the range
// includes the block's trailing newline so consecutive blocks abut.
struct SourceBlock {
  BlockKind kind = BlockKind::Paragraph;
  std::size_t start = 0;
  std::size_t end = 0;
  // The editable payload: what remains after "## ", "- [ ] ", "> " and friends.
  // Everything outside [contentStart, contentEnd) inside the block is marker.
  std::size_t contentStart = 0;
  std::size_t contentEnd = 0;
  int level = 0;      // heading level, 1-6
  int listDepth = 0;  // nesting level derived from leading indentation
  int ordinal = 0;    // the number an ordered item was written with
  bool ordered = false;
  bool checked = false;
  std::string info;   // fence language, or callout kind ("NOTE", "WARNING", ...)
};

// Line-based, single pass. The returned blocks cover `source` with no gaps and
// no overlaps.
std::vector<SourceBlock> scanBlocks(std::string_view source);

// The same scan into a vector the caller owns, which is cleared first but keeps
// its capacity. A rescan happens on every keystroke and produces almost exactly
// as many blocks as the last one did, so handing the previous run's storage back
// turns a per-keystroke reallocation of the whole block list -- on a 200 KB note,
// two growth steps copying 800 KB of blocks -- into nothing at all.
void scanBlocksInto(std::string_view source, std::vector<SourceBlock>* out);

// Asked of an offset the scan has just reached: "did the previous scan start a
// block here too?" See `scanBlocksFrom`.
using ResumeAt = std::function<bool(std::size_t)>;

// The scan resumed part way through a buffer, appending to `out` rather than
// clearing it, and allowed to stop before the end.
//
// `from` must be an offset the scan would itself have started a block at. That
// is not a restriction the scanner can check, and it is also not a hard one to
// satisfy: the scan carries no state between blocks -- every block is decided
// from its own first byte forward -- so *every* block boundary of a previous
// scan over the same bytes qualifies.
//
// The scan stops at the first block boundary within the last `tailBytes` of the
// buffer that `resume` accepts, and returns the offset it stopped at, or
// `source.size()` if it ran to the end. `tailBytes` is the caller's own cheap
// precondition -- how many bytes at the end of the buffer it knows are
// unchanged -- so `resume`, which is a search, is asked only where it can say
// yes. Given both, an edit costs a scan of the edit rather than of the document:
// resume where the change starts, stop where the two scans agree again, and
// take the rest of the blocks from the previous list.
std::size_t scanBlocksFrom(std::string_view source, std::size_t from, std::size_t tailBytes,
                           const ResumeAt& resume, std::vector<SourceBlock>* out);

// The blocks a caller is reading, without saying who owns them. A span rather
// than a `const vector&` because the two callers that matter hold different
// things: the layout keeps a `vector<SourceBlock>` it splices per keystroke, and
// an edit borrows a window of it. A vector converts implicitly, so nothing that
// hands one over had to change.
using BlockSpan = std::span<const SourceBlock>;

// Index of the block owning `offset`. Offsets on a block boundary belong to the
// block that starts there; `source.size()` belongs to the last block.
std::size_t blockIndexAt(BlockSpan blocks, std::size_t offset);

bool isListKind(BlockKind kind);

// Consecutive `>` lines are one quote, or one callout, on screen: each line
// stays its own block, and the container is drawn once over the whole run. A
// `[!KIND]` line always starts a new one.
bool startsQuoteRun(BlockSpan blocks, std::size_t index);
bool endsQuoteRun(BlockSpan blocks, std::size_t index);

}
