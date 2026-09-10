#include "doc/Layout.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

// The incremental half of laying a document out: `Layout.cpp` is the per-block
// work -- styling, staging, placement -- and this is the pass that decides
// which blocks need any of it, which is the only part that has to reason about
// what the *previous* layout said.
//
// They were one 1,700-line file. Splitting them is not a matter of size: it is
// that a reader with a question about one of the two had to hold both, when the
// incremental machinery and the block layout share nothing but a class.
// `UpdatePass` below explains its own shape.

namespace micronotes::doc {
namespace {

}

// One call of `update`, with the five phases as its methods.
//
// `update` was 353 lines and one algorithm: decide whether the standing
// partition still describes the source, absorb the edit, align the placement to
// the new indexing, walk the dirty ranges, carry the outstanding shift down.
// Each phase read and wrote locals the next one depended on -- `head`, `tail`,
// `patchable`, `geometry`, `count`, `previousCount`,
// `shift`, `pendingTop`, `pendingRows`, `settled` -- which is exactly the shape
// that says a function is one algorithm rather than several: extracting a phase
// meant passing eight or nine of them.
//
// So the state is the carrier and the phases are its methods. What made getting
// this right delicate is the ownership, and the rule is one line: **the pass
// owns nothing.** The standing arrays are the layout's -- `placed_`, `flags_`,
// `liveKeys_`, `lineStart_`, `dirty_` are patched in place through
// `doc_`, never copied in and back out -- and what lives here is only the
// bookkeeping that was on the stack before. A carrier holding a generation of
// the document would be the function it replaced with a copy of the note added.
//
// It lives for one call and holds `doc_` and `opts_` by reference for that
// reason. `incoming_` is the caller's buffer, which is deliberately not
// `doc_.source_`: telling those two apart is what the whole first phase is
// about.
class DocumentLayout::UpdatePass {
public:
  UpdatePass(DocumentLayout& doc, std::string_view source, const LayoutOptions& options)
      : doc_(doc), incoming_(source), opts_(options) {}

  void run() {
    geometry_ = geometryKey(opts_);
    previousCount_ = doc_.blocks_.size();
    // A caller that stamps its buffer is believed; one that does not gets the
    // memcmp. The stamp is checked first so the common case -- an idle frame
    // over an unedited note -- does not touch the document at all.
    sourceStamped_ = opts_.sourceRevision != 0 && doc_.built_ &&
                     opts_.sourceRevision == doc_.sourceRevision_;
    // Whether the standing placement can be *patched* rather than rebuilt. The
    // geometry seeds every cache key, so a layout built under a different one
    // shares nothing with this call; and the three parallel arrays have to
    // describe the blocks standing now, or there is nothing to patch.
    patchable_ = doc_.built_ && geometry_ == doc_.geometryHash_ &&
                 doc_.flags_.size() == previousCount_ &&
                 doc_.liveKeys_.size() == previousCount_ &&
                 doc_.placed_.size() == previousCount_ &&
                 doc_.lineStart_.size() == previousCount_ + 1;

    // The blocks whose entry this call can have moved. Collected as ranges,
    // sorted and merged below; everything outside them is provably identical to
    // what is already standing, which is the whole basis of the patch.
    doc_.dirty_.clear();

    if(!absorbSource()) return;

    doc_.options_ = opts_;
    count_ = doc_.blocks_.size();
    // The carried-over ends say which blocks kept their entry, and that is only
    // a claim about a placement there is one of. Without one, nothing carries.
    if(!patchable_) {
      head_ = 0;
      tail_ = 0;
    }
    shift_ = static_cast<std::ptrdiff_t>(count_) - static_cast<std::ptrdiff_t>(previousCount_);
    // See `deadEntryKey`. Deliberately narrower than `!patchable_`: that is
    // also false with no standing layout at all, and with arrays of the wrong
    // length, and in neither case does `liveKeys_[i]` name anything.
    recyclable_ = doc_.built_ && geometry_ != doc_.geometryHash_ && count_ == previousCount_ &&
                  doc_.liveKeys_.size() == count_;

    const perf::ScopeTimer placeTimer("layout.update.place_blocks");
    alignPlacement();
    doc_.mergeDirtyRanges();
    walkDirtyRanges();
    publish();
  }

private:
  void markDirty(std::size_t low, std::size_t high) {
    if(low == kNone) return;
    doc_.dirty_.emplace_back(low, high == kNone ? low : high);
  }

  // --- phase one: does the standing partition still describe the source? ----
  //
  // False when the call is already answered: nothing the layout depends on
  // moved, so every byte this would have copied, scanned, hashed and walked
  // would have reproduced what is already sitting in `placed_`.
  bool absorbSource() {
    // Identical bytes mean an identical partition, so `blocks_` still describes
    // this source. That is the common case by a wide margin: the page re-lays
    // the note out once per frame whether or not anything happened, and a
    // scroll is every frame with nothing happening.
    if(sourceStamped_ || doc_.sourceMatches(incoming_)) return absorbUnchangedSource();
    absorbEdit();
    return true;
  }

  bool absorbUnchangedSource() {
    if(doc_.canReuse(opts_, geometry_)) {
      // Before this returned early it was the single largest cost in a frame,
      // and on a scroll -- where by definition only the viewport moved -- it was
      // the whole frame's work.
      perf::addCounter(perf::CounterId::LayoutUnchangedUpdates);
      // The predicates are fresh closures every frame even when their answers
      // are not, so the stored options have to take them, or a later query would
      // call through a capture that has gone.
      doc_.options_ = opts_;
      doc_.lastRelaid_ = 0;
      return false;
    }
    // The width moved, so the blocks have to be placed again. The scan and the
    // copy do not: those are the document, and the document is what did not
    // change. Every block maps to itself.
    head_ = doc_.blocks_.size();
    return true;
  }

  void absorbEdit() {
    // What the edit did, measured against the buffer the layout is standing on
    // rather than against a copy of it. Taking this window first is what lets
    // everything below be a patch: the source, the block list and the placement
    // are all carried forward through it instead of rebuilt.
    const EditWindow window = matchEdges(doc_.source_, incoming_, doc_.claimFor(opts_));
    const std::size_t previousBytes = doc_.source_.size();
    spliceSource(window, previousBytes);
    {
      const perf::ScopeTimer scanTimer("layout.update.scan_blocks");
      doc_.rescan(window, previousBytes, &head_, &tail_);
    }
    perf::addCounter(perf::CounterId::LayoutBlocksScanned, doc_.blocks_.size());
    const std::size_t count = doc_.blocks_.size();
    // The blocks between the two carried-over ends are the edit itself, and the
    // block either side of them can have changed which quote run it belongs to
    // -- that is the one flag decided by a neighbour rather than by the block.
    markDirty(head_ > 0 ? head_ - 1 : 0, std::min(count - tail_, count - 1));
  }

  // The layout keeps its own copy of the buffer because every run of every
  // cached block points into it, and the caller's buffer is not the layout's to
  // hold. Keeping that copy current used to `assign` the whole note on every
  // keystroke -- 200 KB per typed character. The window says which bytes
  // actually moved, so this writes those and memmoves what follows them.
  void spliceSource(const EditWindow& window, std::size_t previousBytes) {
    const std::size_t from = window.prefix;
    const std::size_t removed = previousBytes - window.suffix - from;
    const std::size_t added = incoming_.size() - window.suffix - from;
    perf::addCounter(perf::CounterId::LayoutSourceBytesCopied, added);
    if(added != removed) {
      perf::addCounter(perf::CounterId::LayoutSourceBytesMoved, window.suffix);
    }
    // `replace` has undefined behaviour if `incoming_` views our own buffer. No
    // caller does that -- and one that handed us back exactly our own bytes
    // would have been answered by `sourceMatches` above -- but a view *into* it
    // would corrupt the copy silently, so it costs two comparisons to say so
    // instead.
    const auto address = [](const char* pointer) {
      return reinterpret_cast<std::uintptr_t>(pointer);
    };
    if(address(incoming_.data()) >= address(doc_.source_.data()) &&
       address(incoming_.data()) <= address(doc_.source_.data()) + doc_.source_.size()) {
      doc_.source_ = std::string(incoming_);
    } else {
      doc_.source_.replace(from, removed, incoming_.data() + from, added);
    }
  }

  // --- phase two: align the standing arrays to the new indexing -------------

  void alignPlacement() {
    if(!patchable_) {
      // Nothing to patch: no standing layout, or one built under another
      // geometry. The walk below rebuilds every entry, which is what no
      // carried-over ends and a dirty range covering the document ask it to do.
      resizeArrays();
      doc_.dirty_.clear();
      doc_.dirty_.emplace_back(0, count_ - 1);
      return;
    }
    if(shift_ == 0) return;
    // The blocks after the edit kept their content, their flags, their key and
    // the layout behind it, and moved by the change in block count -- so moving
    // them is a memmove of four parallel arrays, where rebuilding them is a hash
    // of every byte and a map probe per block.
    const std::size_t oldTail = previousCount_ - tail_;
    if(shift_ > 0) {
      resizeArrays();
      std::move_backward(doc_.placed_.begin() + oldTail, doc_.placed_.begin() + previousCount_,
                         doc_.placed_.end());
      std::move_backward(doc_.flags_.begin() + oldTail, doc_.flags_.begin() + previousCount_,
                         doc_.flags_.end());
      std::move_backward(doc_.liveKeys_.begin() + oldTail, doc_.liveKeys_.begin() + previousCount_,
                         doc_.liveKeys_.end());
      std::move_backward(doc_.lineStart_.begin() + oldTail,
                         doc_.lineStart_.begin() + previousCount_ + 1, doc_.lineStart_.end());
      return;
    }
    const std::size_t newTail = count_ - tail_;
    std::move(doc_.placed_.begin() + oldTail, doc_.placed_.begin() + previousCount_,
              doc_.placed_.begin() + newTail);
    std::move(doc_.flags_.begin() + oldTail, doc_.flags_.begin() + previousCount_,
              doc_.flags_.begin() + newTail);
    std::move(doc_.liveKeys_.begin() + oldTail, doc_.liveKeys_.begin() + previousCount_,
              doc_.liveKeys_.begin() + newTail);
    std::move(doc_.lineStart_.begin() + oldTail, doc_.lineStart_.begin() + previousCount_ + 1,
              doc_.lineStart_.begin() + newTail);
    resizeArrays();
  }

  // The four parallel arrays and the row index, to the block count this call
  // settled on. `lineStart_` carries one extra: the document's total row count.
  void resizeArrays() {
    doc_.placed_.resize(count_);
    doc_.flags_.resize(count_);
    doc_.liveKeys_.resize(count_);
    doc_.lineStart_.resize(count_ + 1);
  }

  // --- phase three: walk what moved, and carry the shift down ---------------

  void walkDirtyRanges() {
    doc_.lastRelaid_ = 0;
    for(const auto& range : doc_.dirty_) {
      const std::size_t low = range.first;
      const std::size_t high = std::min(range.second, count_ - 1);
      if(low > high) continue;
      carryShiftTo(low);
      relayRange(low, high);
      settled_ = high + 1;
    }
    carryShiftToEnd();
  }

  // Carry the outstanding shift down to the start of the next dirty range.
  // Positions only; these blocks' entries are untouched.
  void carryShiftTo(std::size_t low) {
    if(pendingTop_ == 0.0f && pendingRows_ == 0) return;
    for(std::size_t i = settled_; i < low; ++i) {
      doc_.placed_[i].top += pendingTop_;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(doc_.lineStart_[i]) + pendingRows_);
    }
    shifted_ += low - settled_;
  }

  // The cache entry block `i` was filed under a moment ago, when that key is
  // provably dead and the entry provably this block's -- zero otherwise, which
  // is what `resolveEntry` reads as "allocate".
  //
  // Both halves have to hold. The key is dead when the *geometry* moved: the
  // geometry seeds every cache key, so no key this call produces can equal one
  // the last call did, and the whole standing generation is unreachable rather
  // than merely stale. And the entry is this block's when the block count did
  // not change, because then `liveKeys_[i]` was not shifted and still names
  // what index `i` resolved to last time.
  //
  // The second half is what makes it worth doing positionally rather than out
  // of a pool. A pooled buffer goes to whichever block asks next, so the
  // largest paragraph's capacity diffuses across every entry and the layout's
  // memory tends towards (block count x largest block): measured, that was
  // +3.8 MB of peak RSS after eight width steps and +6.0 MB after sixty, and
  // it kept climbing. Block `i` taking back block `i`'s own arrays is an exact
  // fit by construction -- the run count is the block's token count, which
  // does not depend on the width at all -- and it ratchets nowhere.
  std::uint64_t deadEntryKey(std::size_t i) const {
    return recyclable_ ? doc_.liveKeys_[i] : 0;
  }

  void relayRange(std::size_t low, std::size_t high) {
    // The first block of the document starts at zero by definition; any other
    // takes its position from the block above, which is settled by now.
    float top = low == 0 ? 0.0f : doc_.placed_[low].top + pendingTop_;
    std::int64_t rows =
      low == 0 ? 0 : static_cast<std::int64_t>(doc_.lineStart_[low]) + pendingRows_;
    for(std::size_t i = low; i <= high; ++i) {
      const Flags flags = doc_.flagsFor(i);
      const BlockLayout* layout = nullptr;
      // A block from one of the carried-over ends is at the index its entry is
      // already filed under -- the alignment above moved the tail's entries to
      // meet it -- so its standing key and layout describe it still.
      const bool carriedOver = i < head_ || i >= count_ - tail_;
      if(carriedOver && doc_.flags_[i] == flags) {
        // An identical block under identical flags has an identical key, and the
        // layout it resolved to last time is still in the cache under it. This
        // is what makes a conservative dirty range cheap: no bytes hashed, and
        // -- the part that actually costs -- no random probe into a map with one
        // entry per block in the note.
        ++keyReused_;
        layout = doc_.placed_[i].layout;
      } else {
        std::uint64_t key = 0;
        layout = doc_.resolveEntry(i, flags, geometry_, &key, &tally_, deadEntryKey(i));
        doc_.liveKeys_[i] = key;
        doc_.flags_[i] = flags;
      }
      doc_.placed_[i].top = top;
      doc_.placed_[i].layout = layout;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(rows);
      top += layout->height;
      rows += static_cast<std::int64_t>(layout->lines.size());
    }
    walked_ += high - low + 1;
    if(high + 1 < count_) {
      // What this range moved everything below it by. Zero is the common case
      // and the whole point: typing a character inside a paragraph that does not
      // rewrap leaves the rest of the document already correct.
      pendingTop_ = top - doc_.placed_[high + 1].top;
      pendingRows_ = rows - static_cast<std::int64_t>(doc_.lineStart_[high + 1]);
    } else {
      doc_.totalHeight_ = top;
      doc_.lineStart_[count_] = static_cast<std::uint32_t>(rows);
      pendingTop_ = 0.0f;
      pendingRows_ = 0;
    }
  }

  void carryShiftToEnd() {
    if(pendingTop_ == 0.0f && pendingRows_ == 0) return;
    for(std::size_t i = settled_; i < count_; ++i) {
      doc_.placed_[i].top += pendingTop_;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(doc_.lineStart_[i]) + pendingRows_);
    }
    shifted_ += count_ - settled_;
    doc_.lineStart_[count_] = static_cast<std::uint32_t>(
      static_cast<std::int64_t>(doc_.lineStart_[count_]) + pendingRows_);
    doc_.totalHeight_ += pendingTop_;
  }

  // --- phase four: post what happened, and stamp what it happened to --------

  void publish() {
    perf::addCounter(perf::CounterId::LayoutBlocksWalked, walked_);
    perf::addCounter(perf::CounterId::LayoutBlocksShifted, shifted_);
    perf::addCounter(perf::CounterId::LayoutBlocksKeyReused, keyReused_);
    perf::addCounter(perf::CounterId::LayoutKeyBytesHashed, tally_.keyBytes);
    perf::addCounter(perf::CounterId::LayoutBlocksRelaid, doc_.lastRelaid_);
    perf::addCounter(perf::CounterId::LayoutCacheHits, tally_.cacheHits);
    perf::addCounter(perf::CounterId::LayoutBlockLayoutsRecycled, tally_.layoutsRecycled);
    perf::addCounter(perf::CounterId::LayoutBlockLayoutsAllocated, tally_.layoutsAllocated);
    perf::addCounter(perf::CounterId::LayoutVisualRows, doc_.lineStart_[count_]);
    perf::addCounter(patchable_ ? perf::CounterId::LayoutPlacementPatches
                                : perf::CounterId::LayoutPlacementRebuilds);

    doc_.geometryHash_ = geometry_;
    doc_.sourceRevision_ = opts_.sourceRevision;
    doc_.built_ = true;

    doc_.sweepLayoutCache();
  }

  DocumentLayout& doc_;
  // The caller's buffer. Deliberately not `doc_.source_`: telling the two apart
  // is what the first phase is about.
  std::string_view incoming_;
  const LayoutOptions& opts_;

  std::uint64_t geometry_ = 0;
  bool recyclable_ = false;
  std::size_t previousCount_ = 0;
  bool sourceStamped_ = false;
  bool patchable_ = false;

  // How many blocks came through this call unchanged at each end of the
  // document. Everything between them is the extent of what moved, and the two
  // numbers together are what used to be a `size_t` per block.
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t count_ = 0;
  std::ptrdiff_t shift_ = 0;

  // What the walk found, posted once by `publish`.
  Tally tally_;
  std::uint64_t keyReused_ = 0;
  std::uint64_t walked_ = 0;
  std::uint64_t shifted_ = 0;
  // What the blocks recomputed so far have added to every position below them,
  // held back rather than applied: a keystroke that does not change its block's
  // height or line count leaves both zero, and then there is nothing below the
  // edit to touch at all.
  float pendingTop_ = 0.0f;
  std::int64_t pendingRows_ = 0;
  std::size_t settled_ = 0;  // every entry below this index is correct
};

void DocumentLayout::update(std::string_view source, const LayoutOptions& options) {
  const perf::ScopeTimer timer("layout.update");
  perf::addCounter(perf::CounterId::LayoutUpdateCalls);
  UpdatePass(*this, source, options).run();
}

}
