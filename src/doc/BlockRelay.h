#pragma once

#include <cstddef>
#include <cstdint>

namespace micronotes::doc {

// What one `DocumentLayout::update` did to the *block partition*, as a value a
// consumer can check rather than trust.
//
// `DocumentLayout::rescan` already splices: it re-derives only the blocks
// around the edit and slides the rest, which is why a keystroke in a 200 KB
// note relays three blocks rather than nine thousand. But it threw the shape of
// that splice away -- `lastRelaidBlocks()` is a *count*, not a range -- so every
// other reader of the partition had to walk all of it to find what moved. The
// outline did exactly that, and it was 30% of a keystroke (TD-50).
//
// Read it as three bands over the blocks standing now:
//
//   [0, headBlocks)                    kept their index, their content and
//                                      their byte offsets. Nothing below
//                                      `headEnd` moved at all.
//   [headBlocks, count - tailBlocks)   were re-derived. Anything a consumer
//                                      had for these is gone.
//   [count - tailBlocks, count)        kept their content and moved by
//                                      `byteShift`. They start at `tailStart`
//                                      now and started at `tailStart -
//                                      byteShift` before.
//
// The two stamps are the point, and they are the same contract as
// `editor::TextEdit`'s: `fromRevision` must be the source revision the consumer
// built its standing value from and `toRevision` the one it is being asked
// about. A consumer that skipped an update, or is on its second within one
// frame, sees a `fromRevision` that is not what it holds and falls back to
// building from scratch -- which is what makes the whole arrangement safe to be
// wrong about.
//
// `toRevision == 0` means "cannot say": an unstamped caller, a full rescan, a
// geometry-only update with no previous partition. Every consumer must handle
// it, and handling it is doing the unbounded thing.
struct BlockRelay {
  std::uint64_t fromRevision = 0;
  std::uint64_t toRevision = 0;
  std::size_t headBlocks = 0;
  std::size_t tailBlocks = 0;
  // The first byte the head does not cover. The same offset in both buffers,
  // because nothing under it moved.
  std::size_t headEnd = 0;
  // Where the carried tail begins, in the buffer standing now. Meaningless when
  // `tailBlocks` is zero -- it reads as the end of the buffer, and the end of
  // the new buffer less `byteShift` is the end of the old one, which every
  // offset in the last block is below. **A consumer must test `tailBlocks`, not
  // this.** Same for `headEnd` and `headBlocks`, where the zero case happens to
  // fall out right and so is the more dangerous of the two to rely on.
  std::size_t tailStart = 0;
  std::ptrdiff_t byteShift = 0;

  bool known() const { return toRevision != 0; }

  // Whether this relay leads from `revision`. The check a consumer owes itself
  // before splicing, spelled once so the four of them cannot spell it four
  // ways.
  bool leadsFrom(std::uint64_t revision) const {
    return known() && revision != 0 && fromRevision == revision;
  }
};

}
