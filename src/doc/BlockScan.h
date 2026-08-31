#pragma once

#include <cstddef>
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

// Index of the block owning `offset`. Offsets on a block boundary belong to the
// block that starts there; `source.size()` belongs to the last block.
std::size_t blockIndexAt(const std::vector<SourceBlock>& blocks, std::size_t offset);

bool isListKind(BlockKind kind);

// Consecutive `>` lines are one quote, or one callout, on screen: each line
// stays its own block, and the container is drawn once over the whole run. A
// `[!KIND]` line always starts a new one.
bool startsQuoteRun(const std::vector<SourceBlock>& blocks, std::size_t index);
bool endsQuoteRun(const std::vector<SourceBlock>& blocks, std::size_t index);

}
