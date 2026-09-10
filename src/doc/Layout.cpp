#include "doc/Layout.h"

#include "doc/Flow.h"
#include "doc/Tokenize.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "core/util/Hash.h"
#include "core/util/StringUtil.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace micronotes::doc {
namespace {

// A block's fingerprint is recomputed for every block on every keystroke, so
// the word-at-a-time loop matters here; it lives in core because three other
// caches want the same one.
using microcore::util::hashBytes;
using microcore::util::hashValue;
using microcore::util::kFnvOffset;

// Air above and below a picture, so it does not sit on the paragraph that named
// it or on the one after.
constexpr float kImageGap = 8.0f;

// How far past one generation of the document the block cache is allowed to
// run before it is swept. Named because the number is a judgement -- see the
// sweep for what a bigger one was measured to cost.
constexpr std::size_t kSpareEntries = 256;

}

void DocumentLayout::setMetrics(Metrics metrics) {
  metrics_ = std::move(metrics);
  cache_.clear();
  // Every cached block layout was measured with the old faces, so the standing
  // layout no longer describes anything. Without this the reuse check below
  // would happily keep it: the source and the geometry are unchanged, and the
  // one thing that did change is not visible in either.
  built_ = false;
  // And the placement has to go with the cache, because every entry in it is a
  // pointer *into* the cache that was just emptied. The only caller happens to
  // call update() on the next line, so nothing reads the placement in between
  // -- but "safe as long as nobody asks" is a use-after-free waiting for a
  // second caller, and dropping it costs nothing on a path that has already
  // thrown the whole document's layout away. Queries then answer from an empty
  // document, which they are all written to do.
  placed_.clear();
  lineStart_.clear();
  liveKeys_.clear();
  flags_.clear();
  totalHeight_ = 0.0f;
}

editor::TextEdit DocumentLayout::claimFor(const LayoutOptions& options) const {
  const auto& claim = options.editedSpan;
  if(claim.fromRevision == 0 || claim.toRevision == 0) return {};
  if(sourceRevision_ == 0 || options.sourceRevision == 0) return {};
  if(claim.fromRevision != sourceRevision_ || claim.toRevision != options.sourceRevision) return {};
  return claim;
}

DocumentLayout::EditWindow DocumentLayout::matchEdges(std::string_view oldSource,
                                                     std::string_view newSource,
                                                     const editor::TextEdit& claim) {
  // A block at a time through `memcmp`, which is vectorised, then a byte at a
  // time to land exactly. Byte-at-a-time throughout was 70 microseconds over a
  // 200 KB note -- as much as the walk this whole comparison exists to shorten.
  constexpr std::size_t kChunk = 64;
  const std::size_t limit = std::min(oldSource.size(), newSource.size());
  EditWindow window;
  // Where the caller says the edit is, when the claim is about these two
  // buffers. The checks are the whole safety argument: the span has to fit
  // inside both buffers, and the bytes it leaves outside itself have to be the
  // same count on each side -- an insertion of n and a deletion of m leave
  // old.size() - oldEnd == new.size() - newEnd, and nothing else does. A claim
  // that fails either test is discarded rather than trusted, and the loops
  // below then start where they always did.
  //
  // Both loops only widen the matched prefix and suffix -- which narrows the
  // window -- so starting them at the claim gives the same answer it gives from
  // zero whenever the claim is true, and the counter says how much reading it
  // saved.
  std::size_t seededPrefix = 0;
  std::size_t seededSuffix = 0;
  if(claim.fromRevision != 0 && claim.toRevision != 0 && claim.start <= claim.oldEnd &&
     claim.start <= claim.newEnd && claim.oldEnd <= oldSource.size() &&
     claim.newEnd <= newSource.size() &&
     oldSource.size() - claim.oldEnd == newSource.size() - claim.newEnd) {
    perf::addCounter(perf::CounterId::LayoutEditSpansUsed);
    window.prefix = std::min(claim.start, limit);
    window.suffix = std::min(oldSource.size() - claim.oldEnd, limit - window.prefix);
    seededPrefix = window.prefix;
    seededSuffix = window.suffix;
  } else {
    perf::addCounter(perf::CounterId::LayoutEditSpansCompared);
  }
  while(window.prefix + kChunk <= limit &&
        std::memcmp(oldSource.data() + window.prefix, newSource.data() + window.prefix, kChunk) == 0) {
    window.prefix += kChunk;
  }
  while(window.prefix < limit && oldSource[window.prefix] == newSource[window.prefix]) {
    ++window.prefix;
  }

  const std::size_t tailLimit = limit - window.prefix;
  window.suffix = std::min(window.suffix, tailLimit);
  while(window.suffix + kChunk <= tailLimit &&
        std::memcmp(oldSource.data() + oldSource.size() - window.suffix - kChunk,
                    newSource.data() + newSource.size() - window.suffix - kChunk, kChunk) == 0) {
    window.suffix += kChunk;
  }
  while(window.suffix < tailLimit &&
        oldSource[oldSource.size() - 1 - window.suffix] ==
          newSource[newSource.size() - 1 - window.suffix]) {
    ++window.suffix;
  }
  perf::addCounter(perf::CounterId::LayoutEditBytesMatched, window.prefix + window.suffix);
  // What the two passes actually read, as against what they concluded. Without
  // a claim the two are the same number; with one, the difference is the note
  // the caller saved us reading. `seededSuffix` can exceed the final suffix,
  // because the clamp against `tailLimit` only ever reduces it.
  perf::addCounter(perf::CounterId::LayoutEditBytesCompared,
                   (window.prefix - seededPrefix) +
                     (window.suffix > seededSuffix ? window.suffix - seededSuffix : 0));
  return window;
}

void DocumentLayout::rescan(const EditWindow& window, std::size_t previousBytes,
                            std::size_t* headOut, std::size_t* tailOut) {
  *headOut = 0;
  *tailOut = 0;
  // A list to splice into has to exist, and has to have described the buffer the
  // window was measured against. `blocks_` always partitions the source it was
  // scanned from, so where the last block ends is the whole test.
  if(blocks_.empty() || blocks_.back().end() != previousBytes) {
    scanBlocksInto(source_, &blocks_);
    return;
  }

  const std::ptrdiff_t byteShift =
    static_cast<std::ptrdiff_t>(source_.size()) - static_cast<std::ptrdiff_t>(previousBytes);

  // Where the scan resumes. A block's classification can depend on the line
  // that follows it -- a paragraph ends because the next line starts something,
  // a table is a table because of the row under it -- so the finest thing the
  // edit can be said to have touched is the line holding its first changed
  // byte, not the byte. Resuming two blocks above that line is one block of
  // margin over what the argument needs, and costs two cache probes.
  //
  // The line is found in the already-patched buffer, which is allowed because
  // `prefix` is by definition the first byte that differs: everything before it
  // reads the same in either generation.
  const std::size_t changed = std::min(window.prefix, previousBytes);
  std::size_t lineAt = 0;
  if(changed > 0) {
    const std::size_t newline = source_.rfind('\n', changed - 1);
    lineAt = newline == std::string::npos ? 0 : newline + 1;
  }
  const std::size_t holder = blockIndexAt(blocks_, lineAt);
  const std::size_t carried = holder >= 2 ? holder - 2 : 0;
  const std::size_t restart = blocks_[carried].start;

  // Where it may stop. Both halves of this matter: `scanBlocksFrom` asks only
  // once the rest of the buffer is bytes the edit left alone, and this says the
  // previous scan was between blocks at the same place. A scan that carries no
  // state, resumed from a shared boundary over identical bytes, produces
  // identical blocks -- so from there on the answer is the list already in hand.
  //
  // The `> restart` is not decoration. A large insertion can reach the
  // untouched tail after scanning fewer bytes than it added, and the matching
  // old offset would then land *above* where the scan resumed -- which would
  // splice the same blocks in twice.
  const auto resumeAt = [&](std::size_t offset) {
    const std::ptrdiff_t was = static_cast<std::ptrdiff_t>(offset) - byteShift;
    if(was <= static_cast<std::ptrdiff_t>(restart)) return false;
    const std::size_t index = blockIndexAt(blocks_, static_cast<std::size_t>(was));
    return blocks_[index].start == static_cast<std::size_t>(was);
  };

  scanned_.clear();
  const std::size_t stopped =
    scanBlocksFrom(source_, restart, window.suffix, resumeAt, &scanned_);
  const std::size_t oldCount = blocks_.size();
  const std::size_t resume =
    stopped < source_.size()
      ? blockIndexAt(blocks_, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(stopped) -
                                                       byteShift))
      : oldCount;

  // Splice: the head stays where it is, the scanned middle replaces the blocks
  // it re-derived, and the tail slides to meet it.
  const std::size_t middle = scanned_.size();
  const std::size_t count = carried + middle + (oldCount - resume);
  if(count > oldCount) {
    blocks_.resize(count);
    std::move_backward(blocks_.begin() + resume, blocks_.begin() + oldCount, blocks_.end());
  } else if(count < oldCount) {
    std::move(blocks_.begin() + resume, blocks_.begin() + oldCount,
              blocks_.begin() + carried + middle);
    blocks_.resize(count);
  }
  std::move(scanned_.begin(), scanned_.end(), blocks_.begin() + carried);
  // The tail's blocks keep everything except where in the buffer they sit, and
  // that moved by exactly the number of bytes the edit added or removed. One
  // integer add per block -- a block's length and its payload are held from its
  // own start, so moving the block moves all four offsets at once -- against a
  // hash of every byte and a map probe each if they were re-derived instead.
  for(std::size_t i = carried + middle; i < count; ++i) {
    SourceBlock& block = blocks_[i];
    block.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(block.start) + byteShift);
  }

  perf::addCounter(perf::CounterId::LayoutBlocksRescanned, middle);
  if(blocks_.empty()) {
    // An emptied buffer is still one block, so there is somewhere to put a
    // caret. Same rule the full scan ends on, and nothing carried over.
    blocks_.emplace_back();
    return;
  }
  *headOut = carried;
  *tailOut = oldCount - resume;
}

std::size_t DocumentLayout::blockIndexFor(std::size_t offset) const {
  if(offset == kNone) return kNone;
  return blockIndexAt(blocks_, std::min(offset, source_.size()));
}

// A memcmp of the whole note, which is O(document) -- but it is one linear pass
// over bytes already in cache, and what it decides is whether to skip the copy,
// the rescan, the per-block key hash and the flat-line rebuild. On a 235 KB note
// that trade is about 12 microseconds against 1.9 milliseconds.
bool DocumentLayout::sourceMatches(std::string_view source) const {
  if(!built_) return false;
  if(source.size() != source_.size()) return false;
  return source.empty() || std::memcmp(source.data(), source_.data(), source.size()) == 0;
}

// Whether the layout already standing is the exact answer to this call.
//
// "Exact" has to cover every input the block cache keys on, because the whole
// point is to skip building those keys -- which, now that a page is only ever
// read, is the geometry hash alone. The source bytes are the caller's
// precondition: this is asked only once `sourceMatches` has said yes.
//
// Two things are deliberately NOT covered, both because the block cache does not
// cover them either, so nothing regresses by skipping them here:
//   - `wikiLinkResolves`. Its answer decides a run's role but is not part of a
//     block's cache key, so a link that starts or stops resolving does not
//     invalidate the cached block today either. It shows up on the next edit.
//   - `metrics_`. Installing new metrics drops the cache and clears `built_`,
//     which is the invalidation.
bool DocumentLayout::canReuse(const LayoutOptions& options, std::uint64_t geometry) const {
  (void)options;
  return geometry == geometryHash_;
}

DocumentLayout::Flags DocumentLayout::flagsFor(std::size_t index) const {
  const SourceBlock& block = blocks_[index];
  Flags flags;
  flags.first = index == 0;
  // A buffer ending in a newline has one more (empty) line to put a caret on.
  flags.trailingLine = index + 1 == blocks_.size() && block.end() == source_.size() &&
                       !source_.empty() && source_.back() == '\n';
  flags.groupFirst = startsQuoteRun(blocks_, index);
  flags.groupLast = endsQuoteRun(blocks_, index);
  return flags;
}

const BlockLayout* DocumentLayout::resolveEntry(std::size_t index, const Flags& flags,
                                                std::uint64_t geometry, std::uint64_t* key,
                                                Tally* tally, std::uint64_t deadKey) {
  const SourceBlock& block = blocks_[index];
  tally->keyBytes += block.end() - block.start;
  std::uint64_t hash =
    hashBytes(geometry, source_.data() + block.start, block.end() - block.start);
  hash = hashValue(hash, block.kind);
  hash = hashValue(hash, block.level);
  hash = hashValue(hash, block.listDepth);
  hash = hashValue(hash, block.ordinal);
  hash = hashValue(hash, block.checked);
  // Hashed as bytes, which is only correct while the struct has no padding in
  // it: padding bytes are indeterminate, so hashing them mixes whatever the
  // stack held into a cache key -- and the failure mode is a block that hashes
  // to two keys under the same flags, which is a cache that quietly stops
  // hitting rather than anything that looks like a bug. Four bools have no
  // padding; adding a wider field to `Flags` would introduce some, and this is
  // what makes that a build error rather than a slow afternoon.
  static_assert(sizeof(Flags) == 4 * sizeof(bool),
                "Flags is hashed as raw bytes and must have no padding");
  hash = hashBytes(hash, &flags, sizeof(flags));
  *key = hash;

  auto found = cache_.find(hash);
  if(found != cache_.end()) {
    ++tally->cacheHits;
    return &found->second;
  }

  ++lastRelaid_;
  // The entry this block was filed under a moment ago, when the caller knows
  // nothing can ask for that key again. A node carries the map's own
  // allocation as well as the layout's two arrays, and `node.key()` is
  // assignable -- which is what lets the entry be re-filed under the new key
  // rather than one being freed and another allocated.
  CacheMap::node_type dead = deadKey == 0 ? CacheMap::node_type {} : cache_.extract(deadKey);
  if(dead.empty()) {
    ++tally->layoutsAllocated;
    // Filed empty and then laid into, rather than laid out and moved in. A
    // `BlockLayout` move is four vector steals and the scalars, and a cold
    // open of the 200 KB note does it ten thousand times for no reason: the
    // node is where the layout is going to live either way.
    BlockLayout& fresh = cache_.emplace(hash, BlockLayout {}).first->second;
    layoutBlockInto(index, flags, fresh);
    return &fresh;
  }
  ++tally->layoutsRecycled;
  dead.key() = hash;
  dead.mapped().reuse();
  layoutBlockInto(index, flags, dead.mapped());
  return &cache_.insert(std::move(dead)).position->second;
}

// Everything about the *shape* a block would be laid out in, as one number.
//
// It seeds every cache key, so a layout built under a different one shares
// nothing with this call -- which is why `patchable` requires it to match. Every
// field of `LayoutOptions` that is not the source itself belongs here, and
// forgetting one is a stale layout kept under a key that no longer describes
// it.
std::uint64_t DocumentLayout::geometryKey(const LayoutOptions& options) {
  std::uint64_t geometry = kFnvOffset;
  geometry = hashValue(geometry, options.width);
  geometry = hashValue(geometry, options.fontScale);
  geometry = hashValue(geometry, options.indentStep);
  geometry = hashValue(geometry, options.listGutter);
  geometry = hashValue(geometry, options.quoteGutter);
  geometry = hashValue(geometry, options.blockSpacing);
  geometry = hashValue(geometry, options.headingSpaceAbove);
  // Same rule as `Flags` below: raw-byte hashed, so it must stay padding-free.
  static_assert(sizeof(TypeMetrics) == 9 * sizeof(float),
                "TypeMetrics is hashed as raw bytes and must have no padding");
  geometry = hashBytes(geometry, &options.type, sizeof(options.type));
  geometry = hashValue(geometry, options.wikiLinkRevision);
  geometry = hashValue(geometry, options.imageMaxHeight);
  geometry = hashValue(geometry, options.imageRevision);
  return geometry;
}

// Sorted and merged, so the walk sees each block at most once and the gaps
// between ranges are real gaps. Adjacent ranges merge as well as overlapping
// ones: a gap of nothing is not a gap worth reconverging across.
void DocumentLayout::mergeDirtyRanges() {
  std::sort(dirty_.begin(), dirty_.end());
  std::size_t ranges = 0;
  for(std::size_t i = 0; i < dirty_.size(); ++i) {
    if(ranges > 0 && dirty_[i].first <= dirty_[ranges - 1].second + 1) {
      dirty_[ranges - 1].second = std::max(dirty_[ranges - 1].second, dirty_[i].second);
    } else {
      dirty_[ranges++] = dirty_[i];
    }
  }
  dirty_.resize(ranges);
}

void DocumentLayout::sweepLayoutCache() {
  // Bounded memory: keep the live generation of block layouts and a small spare.
  // The sweep is where a resize spends its worst frame, because it frees layouts
  // in bulk -- every line, run and run string of the ones it drops -- so it gets
  // its own timer rather than hiding inside the update's.
  //
  // The ceiling was `blocks * 3 + 256` on the theory that three generations buy
  // back the case where a key returns: an undo, a retype, a window dragged back
  // to a width it just left. Measured on the 200 KB fixture, interleaved, it
  // buys nothing and costs a lot. At one generation `layout.blocks_relaid` is
  // *identical* -- not one extra block was laid out, because on that workload no
  // key ever came back -- while peak RSS goes from 55.1 MB to 28.7 MB.
  //
  // The other half is the shape of the work rather than the amount. Fourteen
  // width steps at three generations are one sweep freeing 26,864 layouts, which
  // is a single 24 ms frame and is the whole of the worst frame of a window drag.
  // At one generation they are nine sweeps of about 5,400 each: 24 ms of `free`
  // in total instead of 11, and no frame over 11 ms. Total work up, spike down,
  // and the spike is the part anyone sees.
  //
  // The spare `kSpareEntries` is what keeps an undo of a keystroke hitting: an
  // edit adds about one key, so a sweep runs at most every `kSpareEntries + 1`
  // of them.
  if(cache_.size() > blocks_.size() + kSpareEntries) {
    const perf::ScopeTimer evictTimer("layout.update.evict_cache");
    perf::addCounter(perf::CounterId::LayoutCacheSweeps);
    liveSorted_ = liveKeys_;
    std::sort(liveSorted_.begin(), liveSorted_.end());
    std::uint64_t evicted = 0;
    for(auto it = cache_.begin(); it != cache_.end();) {
      if(std::binary_search(liveSorted_.begin(), liveSorted_.end(), it->first)) {
        ++it;
      } else {
        ++evicted;
        it = cache_.erase(it);
      }
    }
    perf::addCounter(perf::CounterId::LayoutCacheEvictions, evicted);
    // No re-pointing of `placed_` afterwards. `unordered_map` is node-based:
    // erasing an element invalidates pointers into that element only, and every
    // key in `liveKeys_` survived the sweep by construction. The loop that used
    // to sit here re-found all ten thousand of them -- one hash probe per block,
    // which is the cost the whole key-reuse path exists to avoid.
  }
}

DocumentLayout::BlockStyle DocumentLayout::styleForBlock(const SourceBlock& block,
                                                        const Flags& flags,
                                                        BlockLayout& out) const {
  const TypeMetrics& type = options_.type;
  BlockStyle style;
  style.base.size = type.body;
  style.padBottom = options_.blockSpacing;
  out.indent = static_cast<float>(block.listDepth) * options_.indentStep;
  out.textLeft = out.indent;

  switch(block.kind) {
    case BlockKind::Heading:
      style.base.size = type.heading[std::clamp<int>(block.level, 1, 6) - 1];
      style.base.strong = true;
      if(!flags.first) style.padTop = options_.headingSpaceAbove;
      break;
    case BlockKind::Bullet:
    case BlockKind::Ordered:
      out.textLeft = out.indent + options_.listGutter;
      break;
    case BlockKind::Todo:
      out.textLeft = out.indent + options_.listGutter;
      // A ticked task is struck through.
      if(block.checked) style.base.strike = true;
      break;
    case BlockKind::Quote:
    case BlockKind::Callout:
      out.textLeft = out.indent + options_.quoteGutter;
      // Padding belongs to the run, not to every line in it, or a three-line
      // callout would be drawn with three lots of air inside its own box.
      style.padTop = flags.groupFirst ? 8.0f : 0.0f;
      style.padBottom = flags.groupLast ? 8.0f : 0.0f;
      // The head of a callout run is its title. It gets no extra height: the
      // `> [!KIND]` line already occupies one, and reserving a band above it
      // as well would leave the box with a blank row over its own name.
      if(block.kind == BlockKind::Callout && flags.groupFirst && block.hasInfo()) {
        out.calloutTitle = true;
        style.base.strong = true;
      }
      break;
    case BlockKind::Code:
      style.base.mono = true;
      style.base.size = type.mono;
      // A band above the code when it names its language, because that is where
      // the name is drawn. It used to be drawn *on* the first line of code,
      // which was invisible while a long line ran off the right of the column
      // and stopped there, and became a permanent collision the moment such a
      // line started wrapping into the column instead.
      style.padTop = block.hasInfo() ? 22.0f : 8.0f;
      style.padBottom = 12.0f;
      break;
    case BlockKind::Divider:
      // A rule needs air on both sides or it reads as an underline.
      style.padTop = 10.0f;
      style.padBottom = 14.0f;
      break;
    case BlockKind::Blank:
      style.padBottom = 0.0f;
      break;
    default:
      break;
  }
  return style;
}

std::vector<Token>& DocumentLayout::nextGroup(std::size_t* count) const {
  if(*count == flowGroups_.size()) flowGroups_.emplace_back();
  std::vector<Token>& group = flowGroups_[(*count)++];
  group.clear();
  return group;
}

std::size_t DocumentLayout::stageSourceLines(const SourceBlock& block,
                                             const RunStyle& base) const {
  const std::string_view source = source_;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;

  const bool fenced = block.kind == BlockKind::Code;
  const std::size_t from = fenced ? block.contentStart() : block.start;
  const std::size_t to = fenced ? block.contentEnd() : block.end();
  bool firstLine = true;
  sourceLinesInto(source, from, to, &sourceLines_);
  for(const auto& [lineStart, lineEnd] : sourceLines_) {
    std::vector<Token>& group = nextGroup(&groupCount);
    if(fenced && firstLine) {
      group.push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                                TextRole::Marker, true, true, -1));
    }
    firstLine = false;
    if(lineEnd > lineStart) {
      group.push_back(makeToken(source, lineStart, lineEnd, base, TextRole::Code, false, false, -1));
    }
    // The newline itself takes no space but must stay addressable.
    const std::size_t tail = std::min(lineEnd + 1, to);
    if(tail > lineEnd) {
      group.push_back(makeToken(source, lineEnd, tail, base, TextRole::Code, false, true, -1));
    }
  }
  if(fenced && firstLine) {
    // `sourceLinesInto` always yields at least one line, so this is unreachable
    // today; it is here so that the opening fence cannot be dropped if it ever
    // yields none.
    nextGroup(&groupCount)
      .push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                           TextRole::Marker, true, true, -1));
  }
  if(fenced && block.end() > block.contentEnd()) {
    // The closing fence takes no width but has to stay addressable, so it
    // rides on the end of the last line rather than claiming one of its own.
    flowGroups_[groupCount - 1]
      .push_back(makeToken(source, block.contentEnd(), block.end(), markerStyle,
                           TextRole::Marker, true, true, -1));
  }
  return groupCount;
}

// The per-byte attribute table for one block's inline spans.
//
// Its own step because it is the one place the *inline* grammar reaches the
// layout: everything the scanner found becomes an attribute on the bytes it
// covers, and everything downstream reads only the table. Fills `out.links` and
// `out.images` on the way, because a span that names a target is the only thing
// that knows the target.
void DocumentLayout::applyInlineSpans(const SourceBlock& block,
                                      const std::vector<SourceSpan>& inlines,
                                      std::vector<Attr>& attrs, BlockLayout& out) const {
  for(const auto& inlineSpan : inlines) {
    // A template rather than a `std::function`: this is called per byte of the
    // span, and through a type-erased call it could not be inlined.
    const auto apply = [&](std::size_t from, std::size_t to, auto&& fn) {
      for(std::size_t i = std::max(from, block.contentStart());
          i < std::min(to, block.contentEnd()); ++i) {
        fn(attrs[i - block.contentStart()]);
      }
    };
    apply(inlineSpan.openStart, inlineSpan.openEnd, [](Attr& a) { a.marker = true; });
    apply(inlineSpan.closeStart, inlineSpan.closeEnd, [](Attr& a) { a.marker = true; });
    switch(inlineSpan.kind) {
      case SpanKind::Strong:
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.strong = true; });
        break;
      case SpanKind::Emphasis:
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.italic = true; });
        break;
      case SpanKind::Strike:
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.strike = true; });
        break;
      case SpanKind::Code:
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) {
          a.mono = true;
          a.role = TextRole::Code;
        });
        break;
      case SpanKind::Image: {
        // The alt text becomes the picture's caption -- it is also all a reader
        // gets when the file cannot be drawn -- and the picture itself is
        // reserved under the block, below.
        out.images.push_back({inlineSpan.target, Rect {}});
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link](Attr& a) {
          a.link = link;
          a.role = TextRole::ImageAlt;
        });
        break;
      }
      case SpanKind::Link:
      case SpanKind::FootnoteRef:
      case SpanKind::Autolink: {
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link](Attr& a) {
          a.link = link;
          a.role = TextRole::Link;
        });
        break;
      }
      case SpanKind::WikiLink: {
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        const bool resolves =
          !options_.wikiLinkResolves || options_.wikiLinkResolves(inlineSpan.target);
        const auto role = resolves ? TextRole::WikiLink : TextRole::WikiLinkUnresolved;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link, role](Attr& a) {
          a.link = link;
          a.role = role;
        });
        break;
      }
      case SpanKind::Escape:
        break;
    }
  }
}

std::size_t DocumentLayout::stageInlineContent(const SourceBlock& block,
                                               const RunStyle& base, BlockLayout& out) const {
  const std::string_view source = source_;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;
  std::vector<Token>& group = nextGroup(&groupCount);

  if(block.contentStart() > block.start) {
    group.push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                              TextRole::Marker, true, true, -1));
  }
  if(block.contentEnd() > block.contentStart()) {
    const perf::ScopeTimer inlineTimer("layout.block.inline_attrs");
    const std::size_t span = block.contentEnd() - block.contentStart();
    const auto& inlines = scanInlinesInto(source.substr(block.contentStart(), span),
                                          block.contentStart(), &inlineScratch_);
    perf::addCounter(perf::CounterId::LayoutInlineSpans, inlines.size());
    if(inlines.empty()) {
      // Nothing marked up, so there is nothing an attribute table could say.
      perf::addCounter(perf::CounterId::LayoutPlainBlocks);
      appendPlainTokens(source, block.contentStart(), block.contentEnd(), base, group);
    } else {
      perf::addCounter(perf::CounterId::LayoutAttrBytes, span);
      // Reassigned rather than reallocated, same as the scan's own buffers: one
      // allocation for a document instead of one per marked-up block.
      std::vector<Attr>& attrs = attrs_;
      attrs.assign(span, Attr {});
      applyInlineSpans(block, inlines, attrs, out);
      {
        const perf::ScopeTimer tokenTimer("layout.block.content_tokens");
        appendContentTokens(source, block.contentStart(), block.contentEnd(), attrs, base,
                            options_.type.mono, group);
      }
    }
  }
  if(block.end() > block.contentEnd()) {
    // The trailing newline is always zero width: it must never push the line.
    group.push_back(makeToken(source, block.contentEnd(), block.end(), markerStyle,
                              TextRole::Marker, true, true, -1));
  }
  return groupCount;
}

void DocumentLayout::layoutBlockInto(std::size_t index, const Flags& flags,
                                     BlockLayout& out) const {
  const perf::ScopeTimer blockTimer("layout.block");
  const SourceBlock& block = blocks_[index];

  out.kind = block.kind;

  const BlockStyle style = styleForBlock(block, flags, out);
  const RunStyle& base = style.base;
  // The block's content, staged as one group per line's worth of source. Two
  // shapes and no third: a fenced code block or a block dropped to raw is the
  // file's own lines, and everything else is one group with the inline grammar
  // applied to it. Decided here because the wrap mark's reserve turns on it.
  const bool asSourceLines = block.kind == BlockKind::Code;
  const float available =
    std::max(40.0f, options_.width - out.textLeft - (asSourceLines ? kWrapMarkReserve : 0.0f));
  const float lineHeight = metrics_.lineHeight ? metrics_.lineHeight(base)
                                               : base.size * options_.type.lineHeightRatio;
  const auto appendTrailingLine = [&](float y) {
    VisualLine line;
    line.y = y;
    line.height = lineHeight;
    line.runBegin = static_cast<std::uint32_t>(out.runs.size());
    TextRun& run = out.runs.emplace_back();
    run.srcStart = block.end() - block.start;
    run.srcEnd = run.srcStart;
    run.rect = {out.textLeft, 0.0f, 0.0f, lineHeight};
    run.style = base;
    line.runEnd = static_cast<std::uint32_t>(out.runs.size());
    out.lines.push_back(line);
    return y + lineHeight;
  };

  // A block the scanner does not model reserves the height md4c will need, and
  // exposes one addressable position at its start.
  if(block.kind == BlockKind::Complex) {
    out.complex = true;
    const float height =
      metrics_.measureComplex ? metrics_.measureComplex(block, options_.width) : lineHeight;
    VisualLine line;
    line.y = style.padTop;
    line.height = std::max(lineHeight, height);
    line.runBegin = static_cast<std::uint32_t>(out.runs.size());
    TextRun& run = out.runs.emplace_back();
    run.srcStart = 0;
    run.srcEnd = block.end() - block.start;
    run.rect = {out.textLeft, 0.0f, 0.0f, line.height};
    run.style = base;
    run.role = TextRole::Body;
    line.runEnd = static_cast<std::uint32_t>(out.runs.size());
    out.lines.push_back(line);
    float bottom = style.padTop + std::max(lineHeight, height);
    if(flags.trailingLine) bottom = appendTrailingLine(bottom);
    out.height = bottom + style.padBottom;
    return;
  }

  const std::size_t groupCount = asSourceLines ? stageSourceLines(block, base)
                                               : stageInlineContent(block, base, out);

  reserveFlowOutput(block, flags, base, available, groupCount, out);

  float bottom = 0.0f;
  {
    const perf::ScopeTimer flowTimer("layout.block.flow");
    // Everything wraps, including a fenced code block and a block dropped to
    // raw. They used to be the two that did not, and a long line in either ran
    // off the right of its own column and stopped there: the clip kept it
    // inside the column, which is the right treatment only if there is a way to
    // follow it, and there was not. A wrapped line carries a mark at the point
    // it broke -- see `VisualLine::continuation` -- so a break the column
    // forced is never mistaken for one the file contains.
    FlowGeometry geometry;
    geometry.base = block.start;
    geometry.textLeft = out.textLeft;
    geometry.width = available;
    geometry.lineHeight = lineHeight;
    geometry.top = style.padTop;
    // Everything wraps, so this is not a parameter any caller varies.
    geometry.wrap = true;
    Flow flow(metrics_, geometry, out, flowScratch_);
    flow.run(flowGroups_, groupCount);
    bottom = flow.bottom();
    perf::addCounter(perf::CounterId::LayoutFlowMeasures, flow.measures());
    perf::addCounter(perf::CounterId::LayoutFlowWordSplits, flow.wordSplits());
  }
  if(flags.trailingLine) bottom = appendTrailingLine(bottom);
  // The pictures the block named, under its text and in source order. Reserved
  // here rather than drawn over the following blocks, so the note scrolls past
  // an image the same way it scrolls past a paragraph and every position query
  // below this block is already right.
  if(!out.images.empty()) bottom = placeImages(out, bottom);
  out.height = bottom + style.padBottom;
}

// Roughly how many lines this is about to wrap into, so the line vector grows
// once rather than doubling its way there. Half the type size is a crude mean
// glyph advance, and being wrong only costs the doubling this avoids.
void DocumentLayout::reserveFlowOutput(const SourceBlock& block, const Flags& flags,
                                       const RunStyle& base, float available,
                                       std::size_t groupCount, BlockLayout& out) const {
  const std::size_t contentBytes =
    block.contentEnd() > block.contentStart() ? block.contentEnd() - block.contentStart() : 0;
  const float inkWidth = static_cast<float>(contentBytes) * base.size * 0.5f;
  out.lines.reserve(static_cast<std::size_t>(inkWidth / std::max(1.0f, available)) + 1);
  // And exactly how many runs, which is not an estimate: the flow emits one run
  // per staged token, plus one more for a trailing line. A word split across a
  // wrap emits more, which is the one case this under-counts and the one case
  // the doubling is right for.
  std::size_t tokens = flags.trailingLine ? 1 : 0;
  for(std::size_t g = 0; g < groupCount; ++g) tokens += flowGroups_[g].size();
  perf::addCounter(perf::CounterId::LayoutTokensStaged, tokens);
  out.runs.reserve(tokens);
}

// The images of a block that has some, laid out down the column from `top`.
// Split out because a block with none -- which is almost every block -- should
// not pay a branch inside a loop for it.
float DocumentLayout::placeImages(BlockLayout& out, float top) const {
  const float column = std::max(40.0f, options_.width - out.indent);
  for(auto& image : out.images) {
    perf::addCounter(perf::CounterId::LayoutImagesMeasured);
    const ImageBox box = metrics_.measureImage
                           ? metrics_.measureImage(image.target, column, options_.imageMaxHeight)
                           : ImageBox {};
    if(box.width <= 0.0f || box.height <= 0.0f) {
      image.rect = {out.indent, top, 0.0f, 0.0f};
      continue;
    }
    image.rect = {out.indent, top + kImageGap, box.width, box.height};
    top = image.rect.y + box.height + kImageGap;
  }
  return top;
}

}
