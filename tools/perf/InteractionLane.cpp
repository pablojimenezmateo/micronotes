#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "doc/Fold.h"
#include "doc/Layout.h"
#include "ui/FoldState.h"

#include <iostream>
#include <string>


namespace micronotes::perfharness {

// What the user actually does to a note: type in it, move about in it, and
// scroll it. Each of these was chosen because it stresses a different part of
// the reuse machinery, and between them they cover the cases where it can be
// wrong as well as the ones where it can be slow.
//
// Every scenario reports allocations per iteration next to the median, because
// the interesting failures here are churn rather than arithmetic: an edit that
// re-allocates a buffer per visual line reads as "fast" on an idle machine and
// as a stutter on a loaded one, and only the allocation count says which of the
// two you are looking at.


// A caret that lands in a block reveals that block's markers, so moving it is a
// relayout even though not one byte changed. It is also the single most common
// thing a keyboard does, and nothing measured it before: every layout budget
// here was an edit, so the arrow keys were free by assumption.
static constexpr std::uint64_t kCaretMoveBudgetMicros = 1000;
// Typing. The budget is the design's: one keystroke in a 200 KB note.
static constexpr std::uint64_t kTypeBudgetMicros = 2000;
// Laying every block of a 200 KB note out from scratch, which is what opening a
// note and what dragging the window edge both do. Deliberately loose: this one
// is meant to catch a change that makes first paint several times worse, not to
// police a few per cent on a machine whose clock moves with whatever else is
// running on it. The allocation count beside it is the tight number.
static constexpr std::uint64_t kColdBudgetMicros = 20000;

bool interactionBudgets(const std::string& base) {
  std::cout << "\n=== what typing, moving and scrolling a 200 KB note cost ===\n";

  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;

  // The live surface stamps its buffer, so the harness has to as well or it
  // measures a path the app never takes. The stamp moves on every mutation,
  // which is exactly the contract LayoutOptions asks callers to keep.
  std::uint64_t revision = 1;
  std::string source = base;
  const auto relayout = [&](std::size_t caret) {
    options.caretOffset = caret;
    options.sourceRevision = revision;
    layout.update(source, options);
  };
  const auto typeAt = [&](std::size_t caret, char c) {
    source.insert(caret, 1, c);
    ++revision;
    relayout(caret + 1);
  };

  relayout(0);
  const std::size_t blocks = layout.blockCount();
  std::cout << "interaction.blocks: " << blocks << "\n";

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Typing at the top, in the middle and at the end. They are three scenarios
  // rather than one because the incremental path is asymmetric: an edit near the
  // top leaves almost nothing above it to carry over and shifts everything
  // below, an edit at the end is the reverse, and an edit in the middle pays
  // half of each. A reuse scheme that only ever gets tested in the middle can be
  // silently O(document) at one end.
  {
    std::size_t caret = source.find("A paragraph");
    if(caret == std::string::npos) caret = 0;
    caret += 2;
    gate("type.near_top",
         measureIterations("type.near_top", 24, [&](int) { typeAt(caret, 'x'); }),
         kTypeBudgetMicros);
  }
  {
    std::size_t caret = source.find("A paragraph", source.size() / 2);
    if(caret == std::string::npos) caret = source.size() / 2;
    caret += 2;
    gate("type.middle", measureIterations("type.middle", 24, [&](int) { typeAt(caret, 'x'); }),
         kTypeBudgetMicros);
  }
  {
    gate("type.at_end",
         measureIterations("type.at_end", 24, [&](int) { typeAt(source.size(), 'x'); }),
         kTypeBudgetMicros);
  }

  // Backspace, which is typing in reverse and takes the other branch of every
  // prefix/suffix comparison.
  {
    std::size_t caret = source.find("A paragraph", source.size() / 2);
    if(caret == std::string::npos) caret = source.size() / 2;
    caret += 8;
    gate("backspace.middle", measureIterations("backspace.middle", 24, [&](int) {
           if(caret == 0 || caret > source.size()) return;
           source.erase(caret - 1, 1);
           --caret;
           ++revision;
           relayout(caret);
         }),
         kTypeBudgetMicros);
  }

  // Enter and its undo. This is the case the byte-level prefix/suffix map is
  // least able to help with, because it changes the block partition: every
  // block below the split shifts by one index as well as by an offset, so the
  // pairing has to come from the end of the document rather than the start.
  {
    std::size_t caret = source.find("A paragraph", source.size() / 3);
    if(caret == std::string::npos) caret = source.size() / 3;
    caret += 8;
    gate("newline.split_and_join", measureIterations("newline.split_and_join", 16, [&](int) {
           source.insert(caret, "\n\n");
           ++revision;
           relayout(caret + 2);
           source.erase(caret, 2);
           ++revision;
           relayout(caret);
         }),
         kTypeBudgetMicros * 2);
  }

  // Arrow keys: the source stands completely still and the caret walks from
  // block to block, revealing one set of markers and hiding another.
  {
    std::vector<std::size_t> stops;
    for(std::size_t i = 0; i < layout.blockCount(); i += std::max<std::size_t>(1, blocks / 64)) {
      stops.push_back(layout.blocks()[i].start);
    }
    gate("caret.block_to_block", measureIterations("caret.block_to_block", 60, [&](int i) {
           relayout(stops[static_cast<std::size_t>(i) % stops.size()]);
         }),
         kCaretMoveBudgetMicros);
  }

  // Scrolling. Two halves, and they fail differently.
  //
  // The first is the relayout a scroll must NOT do: the buffer, the caret and
  // the geometry all stand still, so the standing layout is already the answer.
  // The second is the work a scroll genuinely does -- asking the layout which
  // blocks are in the band that just came into view, and where the caret and a
  // clicked point land in it. Nothing measured that side before, and it is the
  // part that has to stay proportional to the window rather than the note.
  {
    gate("scroll.idle_frame",
         measureIterations("scroll.idle_frame", 120, [&](int) { relayout(0); }), 500);

    const float total = layout.totalHeight();
    const float viewport = 1000.0f;
    gate("scroll.viewport_queries", measureIterations("scroll.viewport_queries", 120, [&](int i) {
           const float top = total > viewport
                               ? static_cast<float>(i) / 120.0f * (total - viewport)
                               : 0.0f;
           const auto [from, to] = layout.blockRange(top, top + viewport);
           float ink = 0.0f;
           for(std::size_t b = from; b < to; ++b) ink += layout.layout(b).height;
           (void)ink;
           (void)layout.offsetAt(300.0f, top + viewport / 2.0f);
           (void)layout.blockAt(top);
           (void)layout.rowsPerHeight(viewport);
         }),
         500);
  }

  // Opening a note. Every block is laid out for the first time, so this is the
  // one scenario here that is meant to be O(document) -- and it is the largest
  // single cost in the app, because it all lands in one frame. The allocation
  // count is the number to watch: the work per block is a tokenize and a wrap,
  // and if each of those grows a buffer from empty the count runs to several per
  // block for no reason a reader of the code would expect.
  {
    gate("open.cold_layout", measureIterations("open.cold_layout", 8, [&](int i) {
      micronotes::doc::DocumentLayout fresh;
      fresh.setMetrics(stubMetrics());
      micronotes::doc::LayoutOptions cold;
      cold.width = 700.0f + static_cast<float>(i);  // defeat any cross-run cache
      fresh.update(source, cold);
    }),
         kColdBudgetMicros);
  }

  // Folding and unfolding a heading. The fold predicate is asked per block, and
  // collapsing a heading changes the height of everything below it, so this is
  // the case a stamped fold revision is meant to keep off the idle path without
  // making the actual toggle slower.
  {
    micronotes::ui::FoldState folds;
    std::uint64_t foldRevision = 1;
    const auto blockList = micronotes::doc::scanBlocks(source);
    std::vector<std::string> headings;
    for(const auto& block : blockList) {
      if(block.kind == micronotes::doc::BlockKind::Heading) {
        headings.push_back(micronotes::doc::foldKey(source, block));
      }
      if(headings.size() >= 8) break;
    }
    options.folded = [&](const micronotes::doc::SourceBlock& block) {
      return folds.folded("perf", micronotes::doc::foldKey(source, block));
    };
    const auto foldRelayout = [&](std::size_t caret) {
      options.caretOffset = caret;
      options.sourceRevision = revision;
      options.foldRevision = foldRevision;
      layout.update(source, options);
    };
    foldRelayout(0);
    if(!headings.empty()) {
      gate("fold.toggle_heading", measureIterations("fold.toggle_heading", 16, [&](int i) {
             folds.toggle("perf", headings[static_cast<std::size_t>(i) % headings.size()]);
             foldRevision = folds.revision() + 1;
             foldRelayout(0);
           }),
           kTypeBudgetMicros);
      // The frame after a toggle, where nothing has moved. A stamped fold
      // revision should make this cost nothing at all; without one it re-asks
      // the predicate once per block and rebuilds a fold key for each ask.
      gate("fold.idle_frame_after_toggle",
           measureIterations("fold.idle_frame_after_toggle", 120, [&](int) { foldRelayout(0); }),
           500);
    }
    options.folded = nullptr;
    options.foldRevision = 0;
    relayout(0);
  }

  // Resizing the window. Every cached block was measured at the old column
  // width, so this one is meant to be expensive -- it is here so that "expensive"
  // stays a number somebody can watch rather than an assumption.
  {
    gate("resize.width_step", measureIterations("resize.width_step", 8, [&](int i) {
      // Every step a width the layout has never seen, which is what dragging an
      // edge does. Cycling a handful of widths measures the block cache instead.
      const float width = 600.0f + static_cast<float>(i) * 7.0f;
      options.width = width;
      relayout(0);
    }),
         kColdBudgetMicros);
    options.width = 700.0f;
    relayout(0);
  }

  return ok;
}

}
