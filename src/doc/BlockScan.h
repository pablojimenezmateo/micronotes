#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::doc {

// The block types the live editing surface models directly. Anything else is
// tagged `Complex` and handed to the md4c render model wholesale, so the
// scanner never has to guess at constructs it cannot round-trip.
enum class BlockKind : std::uint8_t {
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

// A block's exact source range. `start`/`end()` partition the buffer; the range
// includes the block's trailing newline so consecutive blocks abut.
//
// **Everything but `start` is relative to it, and packed.** One block used to be
// 88 bytes -- four absolute `std::size_t` offsets and an owned `std::string` --
// and both of those cost on the two paths an edit still walks:
//
//   * a block-count change memmoves the tail of the array. 9,612 blocks at 88
//     bytes is 846 KB for one pressed Return; at 40 it is 385.
//   * every edit shifts the tail's offsets, and with four absolute offsets that
//     was four adds per block. Relative content offsets make it one, because
//     moving a block moves only where it starts.
//
// `info` -- a fence language or a callout kind, empty for almost every block --
// is a span of the source for the same reason. It cannot be a `string_view`:
// the small-string optimisation puts a short one's bytes inside the string
// object, so a view into a source buffer that gets swapped out dangles. A span
// resolved against whatever buffer the caller is holding cannot.
//
// A block's length is a `std::uint32_t`, which caps one block at 4 GB. A note
// is a file someone typed.
struct SourceBlock {
  // Absolute, and the only field that is.
  std::size_t start = 0;
  std::uint32_t length = 0;
  // The editable payload: what remains after "## ", "- [ ] ", "> " and friends.
  // Everything outside [contentStart(), contentEnd()) inside the block is
  // marker. Held from `start`.
  std::uint32_t contentBegin = 0;
  std::uint32_t contentLength = 0;
  // The fence language or the callout kind, as a span from `start`. Read with
  // `info(source)`. Sixteen bits each because both live on the block's *first*
  // line -- ```` ```cpp ```` and `> [!NOTE]` -- so an offset past 64 KB means a
  // first line longer than any editor would show, and `setInfo` drops it rather
  // than truncating it into a different language. That is what takes the struct
  // to 40 bytes, which is what puts six blocks on a cache line instead of five.
  std::uint16_t infoBegin = 0;
  std::uint16_t infoLength = 0;
  std::int32_t ordinal = 0;    // the number an ordered item was written with
  std::int16_t listDepth = 0;  // nesting level derived from leading indentation
  std::uint8_t level = 0;      // heading level, 1-6
  BlockKind kind = BlockKind::Paragraph;
  // The punctuation a list item was written with: `-`, `*` or `+` for a bullet
  // or a to-do, `.` or `)` for an ordered item, zero for everything else. It is
  // what lets a continuation keep the marker the author chose instead of
  // imposing one -- pressing Return under `* a` owes them `* `, not `- `. It
  // sits in what was `bool ordered`, which said nothing `kind == Ordered` did
  // not already say, so the struct is the same 40 bytes it was.
  char listMarker = 0;
  bool checked = false;

  std::size_t end() const {
    return start + length;
  }

  std::size_t contentStart() const {
    return start + contentBegin;
  }

  std::size_t contentEnd() const {
    return start + contentBegin + contentLength;
  }

  bool hasInfo() const {
    return infoLength != 0;
  }

  // The buffer must be the one this block was scanned from, which is the same
  // precondition every other offset here carries.
  std::string_view info(std::string_view source) const {
    return source.substr(start + infoBegin, infoLength);
  }

  void setEnd(std::size_t value) {
    length = static_cast<std::uint32_t>(value - start);
  }

  // The payload in one call, which is how the scanner writes it: it tracks the
  // two offsets as absolutes while it decides what the block is and closes them
  // once, so the packing costs one subtraction per block rather than one per
  // decision. An end before the start is an empty payload, which is what a
  // divider and a blank line have.
  void setContent(std::size_t begin, std::size_t stop) {
    contentBegin = static_cast<std::uint32_t>(begin - start);
    contentLength = stop > begin ? static_cast<std::uint32_t>(stop - begin) : 0;
  }

  void setInfo(std::size_t begin, std::size_t stop) {
    const std::size_t offset = begin - start;
    const std::size_t span = stop - begin;
    if(offset > 0xFFFF || span > 0xFFFF) return;
    infoBegin = static_cast<std::uint16_t>(offset);
    infoLength = static_cast<std::uint16_t>(span);
  }
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
