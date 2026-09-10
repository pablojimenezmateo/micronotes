#include "doc/Layout.h"

#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Asking a laid-out document where things are.
//
// The other half of `DocumentLayout`, and split off because it is a different
// kind of code with a different reason to be careful. `Layout.cpp` is the
// incremental machinery: what the edit was, which blocks it can have moved, and
// how little of the placement has to be rebuilt. Nothing here rebuilds
// anything. Every function is a pure question about `blocks_`, `placed_` and
// `lineStart_` -- where the caret goes, what offset a point names, which rows a
// band covers -- and the arithmetic in it is off-by-one country rather than
// cache-invalidation country.
//
// They shared a translation unit of 1,867 lines and no more than that. `../
// microide` splits `TextViewport` across six files the same way, by concern
// rather than by size, which is the shape this follows: the class is one class
// and its state is private to it, but a reader after the caret arithmetic
// should not have to walk past the placement patch to reach it.
//
// The two searches below rest on an invariant `LayoutTests` pins over a corpus:
// a block's runs are monotonically non-decreasing in `srcStart` and `srcEnd`.
namespace micronotes::doc {

const BlockLayout* DocumentLayout::layoutForOffset(std::size_t offset, std::size_t* blockIndex) const {
  if(placed_.empty()) return nullptr;
  const std::size_t index = blockIndexAt(blocks_, offset);
  if(blockIndex) *blockIndex = index;
  return placed_[index].layout;
}

// A block's runs are monotonically non-decreasing in `srcStart` and in
// `srcEnd`: `Flow` emits them in group order, groups are filled in source
// order, `splitWord` emits its pieces in order, the hidden opening fence is
// pushed in front of the first line's content, and `appendTrailingLine` appends
// the largest offset last. `LayoutTests` asserts that invariant over a corpus,
// because the two searches below depend on it.
//
// The line owning run `index`. Lines are emitted in order and own contiguous
// half-open ranges of the block's run array, so the owner is a binary search.
// An empty line at `index` is stepped over, which is what the linear scan did.
namespace {

const VisualLine* lineOwningRun(const BlockLayout& layout, std::size_t index,
                                std::uint64_t& probes) {
  std::size_t lo = 0;
  std::size_t hi = layout.lines.size();
  while(lo < hi) {
    ++probes;
    const std::size_t mid = lo + (hi - lo) / 2;
    if(layout.lines[mid].runEnd <= index) lo = mid + 1;
    else hi = mid;
  }
  return lo == layout.lines.size() ? nullptr : &layout.lines[lo];
}

}

Rect DocumentLayout::caretRect(std::size_t offset) const {
  const perf::ScopeTimer timer("layout.caret_rect");
  Rect rect {0.0f, 0.0f, 2.0f, options_.type.body * options_.type.lineHeightRatio};
  std::size_t blockIndex = 0;
  const BlockLayout* layout = layoutForOffset(offset, &blockIndex);
  if(!layout || layout->lines.empty()) return rect;
  const float top = placed_[blockIndex].top;
  // Cached runs address their own block, so the caret offset comes down to it.
  const std::size_t local = offset - blocks_[blockIndex].start;

  // The first run that has not already ended at or before the caret. Every run
  // before it ended earlier, so the last of those is the linear scan's
  // `before`; every run after it starts later, so none of them can contain the
  // caret either. One partition point answers both questions.
  std::uint64_t probes = 0;
  std::size_t lo = 0;
  std::size_t hi = layout->runs.size();
  while(lo < hi) {
    ++probes;
    const std::size_t mid = lo + (hi - lo) / 2;
    if(layout->runs[mid].srcEnd <= local) lo = mid + 1;
    else hi = mid;
  }

  std::size_t chosen = layout->runs.size();
  if(lo < layout->runs.size() && layout->runs[lo].srcStart <= local) chosen = lo;
  else if(lo > 0) chosen = lo - 1;   // the last run that ended before the caret

  const TextRun* best = chosen < layout->runs.size() ? &layout->runs[chosen] : nullptr;
  const VisualLine* bestLine =
      best ? lineOwningRun(*layout, chosen, probes) : nullptr;
  perf::addCounter(perf::CounterId::LayoutCaretQueries);
  perf::addCounter(perf::CounterId::LayoutCaretProbes, probes);
  if(!best || !bestLine) {
    bestLine = &layout->lines.front();
    rect.x = layout->textLeft;
    rect.y = top + bestLine->y;
    rect.h = bestLine->height;
    return rect;
  }

  float x = best->rect.x;
  if(local > best->srcStart && !best->text.empty()) {
    const std::size_t take = std::min(local - best->srcStart, best->text.size());
    x += metrics_.measure(std::string_view(best->text).substr(0, take), best->style);
  } else if(local >= best->srcEnd) {
    x = best->rect.x + best->rect.w;
  }
  rect.x = x;
  rect.y = top + bestLine->y;
  rect.h = bestLine->height;
  return rect;
}

std::size_t DocumentLayout::offsetAt(float x, float y) const {
  if(flatLineCount() == 0) return 0;
  const auto [blockIndex, lineIndex] = flatLineAt(flatLineAtY(y));
  const BlockLayout& layout = *placed_[blockIndex].layout;
  const VisualLine& line = layout.lines[lineIndex];
  const SourceBlock& block = blocks_[blockIndex];

  const auto runs = layout.runsOf(line);
  const TextRun* chosen = nullptr;
  for(const auto& run : runs) {
    if(run.text.empty()) continue;
    if(!chosen || x >= run.rect.x) chosen = &run;
  }
  if(!chosen) {
    const std::size_t content = block.contentStart() - block.start;
    for(const auto& run : runs) {
      if(run.srcStart >= content) return block.start + run.srcStart;
    }
    return block.start + (runs.empty() ? 0 : runs.front().srcStart);
  }
  if(x <= chosen->rect.x) return block.start + chosen->srcStart;

  float pen = chosen->rect.x;
  std::size_t i = 0;
  while(i < chosen->text.size()) {
    const std::size_t next = util::nextBoundary(chosen->text, i);
    const float width = metrics_.measure(std::string_view(chosen->text).substr(i, next - i), chosen->style);
    if(x < pen + width / 2.0f) return block.start + chosen->srcStart + i;
    pen += width;
    i = next;
  }
  return block.start + chosen->srcStart + chosen->text.size();
}

// The rect one visual line of a selection paints, or nothing when the line holds
// none of it. Shared by the three entry points below, which differ only in which
// lines they ask about.
std::optional<Rect> DocumentLayout::selectionRectFor(std::size_t block, const VisualLine& line,
                                                     std::size_t from, std::size_t to) const {
  const BlockLayout& layout = *placed_[block].layout;
  const std::size_t base = blocks_[block].start;
  float left = 0.0f;
  float right = 0.0f;
  bool any = false;
  for(const auto& run : layout.runsOf(line)) {
    if(run.text.empty()) continue;
    const std::size_t runStart = base + run.srcStart;
    const std::size_t runEnd = base + run.srcEnd;
    if(runEnd <= from || runStart >= to) continue;
    const std::size_t a = std::max(from, runStart);
    const std::size_t b = std::min(to, runEnd);
    float x0 = run.rect.x;
    float x1 = run.rect.x + run.rect.w;
    if(a > runStart) {
      x0 += metrics_.measure(
        std::string_view(run.text).substr(0, std::min(a - runStart, run.text.size())), run.style);
    }
    if(b < runEnd) {
      x1 = run.rect.x + metrics_.measure(
                          std::string_view(run.text).substr(0, std::min(b - runStart, run.text.size())),
                          run.style);
    }
    if(!any) {
      left = x0;
      right = x1;
      any = true;
    } else {
      left = std::min(left, x0);
      right = std::max(right, x1);
    }
  }
  if(!any) return std::nullopt;
  return Rect {left, placed_[block].top + line.y, std::max(2.0f, right - left), line.height};
}

void DocumentLayout::selectionRectsInto(std::size_t from, std::size_t to, float bandTop,
                                        float bandBottom, std::vector<Rect>* out) const {
  std::vector<Rect>& rects = *out;
  rects.clear();
  if(from > to) std::swap(from, to);
  if(from == to || placed_.empty()) return;
  // Two bounds, and the selection needs both. Only a block the range overlaps
  // can contribute a rect, and blocks are ordered by source offset, so the
  // source ends the walk. Only a block the band reaches can contribute a
  // *visible* one, and blocks tile the document in order, so `blockRange` is a
  // binary search for that end. Without the second, a selection is O(document)
  // per frame however little of it is on screen.
  const auto [bandFirst, bandLast] = blockRange(bandTop, bandBottom);
  std::size_t i = std::max(blockIndexFor(from), bandFirst);
  for(; i < bandLast && i < blocks_.size() && blocks_[i].start < to; ++i) {
    // And the same argument again one level down, for the rows inside a block:
    // a fenced code block is a single block that can be thousands of rows long,
    // so a band that stops at the block boundary stops one level too early.
    for(const VisualLine& line : placed_[i].layout->lines) {
      const float top = placed_[i].top + line.y;
      if(top + line.height <= bandTop) continue;
      if(top >= bandBottom) break;
      if(const auto rect = selectionRectFor(i, line, from, to)) rects.push_back(*rect);
    }
  }
}

std::optional<std::pair<Rect, Rect>> DocumentLayout::selectionEnds(std::size_t from,
                                                                  std::size_t to) const {
  if(from > to) std::swap(from, to);
  if(from == to || placed_.empty()) return std::nullopt;
  const std::size_t first = blockIndexFor(from);
  std::size_t last = blockIndexFor(to == 0 ? to : to - 1);
  if(last < first) last = first;

  std::optional<Rect> front;
  for(std::size_t i = first; !front && i <= last && i < blocks_.size(); ++i) {
    for(const VisualLine& line : placed_[i].layout->lines) {
      front = selectionRectFor(i, line, from, to);
      if(front) break;
    }
  }
  if(!front) return std::nullopt;
  std::optional<Rect> back;
  for(std::size_t i = last + 1; !back && i-- > first;) {
    const BlockLayout& layout = *placed_[i].layout;
    for(std::size_t l = layout.lines.size(); !back && l-- > 0;) {
      back = selectionRectFor(i, layout.lines[l], from, to);
    }
  }
  return std::make_pair(*front, back ? *back : *front);
}

std::vector<Rect> DocumentLayout::selectionRects(std::size_t from, std::size_t to) const {
  std::vector<Rect> rects;
  selectionRectsInto(from, to, -std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(), &rects);
  return rects;
}

std::optional<std::size_t> DocumentLayout::blockAt(float y) const {
  if(placed_.empty()) return std::nullopt;
  if(y < 0.0f || y > totalHeight_) return std::nullopt;
  // Same tiling argument as `blockRange` below, which was already binary
  // searching while this walked: blocks cover [top_i, top_{i+1}) in order, so
  // the block at `y` is the last one starting at or before it. A block of zero
  // height shares its successor's top and loses the tie, which is what the
  // linear scan's "skip anything y does not fit inside" achieved.
  const auto after = std::upper_bound(placed_.begin(), placed_.end(), y,
                                      [](float value, const Placed& placed) {
                                        return value < placed.top;
                                      });
  if(after == placed_.begin()) return 0;
  return static_cast<std::size_t>(after - placed_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> DocumentLayout::blockRange(float top, float bottom) const {
  if(placed_.empty() || bottom < top) return {0, 0};
  // Blocks tile the document: block i covers [top_i, top_{i+1}), and a block of
  // zero height shares its neighbour's top. So the first block on screen is
  // the last one starting at or before
  // `top`, and the range ends at the first one starting after `bottom`.
  const auto byTop = [](const Placed& placed, float value) { return placed.top < value; };
  auto first = std::lower_bound(placed_.begin(), placed_.end(), top, byTop);
  if(first != placed_.begin()) --first;
  const auto last = std::upper_bound(placed_.begin(), placed_.end(), bottom,
                                     [](float value, const Placed& placed) {
                                       return value < placed.top;
                                     });
  return {static_cast<std::size_t>(first - placed_.begin()),
          static_cast<std::size_t>(last - placed_.begin())};
}

std::size_t DocumentLayout::flatLineCount() const {
  return lineStart_.empty() ? 0 : lineStart_.back();
}

std::pair<std::size_t, std::size_t> DocumentLayout::flatLineAt(std::size_t flat) const {
  // `lineStart_` is non-decreasing and starts at zero, so the owning block is
  // the last one whose start is at or below `flat`. A block with no rows
  // repeats its predecessor's value, and upper_bound steps over the whole run
  // of them in one go.
  const auto after = std::upper_bound(lineStart_.begin(), lineStart_.end(),
                                      static_cast<std::uint32_t>(flat));
  const auto block = static_cast<std::size_t>(after - lineStart_.begin()) - 1;
  return {block, flat - lineStart_[block]};
}

float DocumentLayout::flatLineTop(std::size_t flat) const {
  const auto [block, line] = flatLineAt(flat);
  return placed_[block].top + placed_[block].layout->lines[line].y;
}

std::size_t DocumentLayout::flatLineAtY(float y) const {
  // Row tops are non-decreasing across the index -- blocks tile the document in
  // order and rows tile their block -- so the linear "keep the last row at or
  // above y" scan this replaces was a hand-rolled binary search over a sorted
  // array, run over every row in the note.
  std::size_t lo = 0;
  std::size_t hi = flatLineCount();
  std::uint64_t probes = 0;
  while(lo < hi) {
    ++probes;
    const std::size_t mid = lo + (hi - lo) / 2;
    if(flatLineTop(mid) <= y) lo = mid + 1;
    else hi = mid;
  }
  perf::addCounter(perf::CounterId::LayoutRowIndexQueries);
  perf::addCounter(perf::CounterId::LayoutRowIndexProbes, probes);
  return lo == 0 ? 0 : lo - 1;
}


}
