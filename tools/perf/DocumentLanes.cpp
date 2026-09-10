#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "doc/Edits.h"
#include "doc/Layout.h"

#include <iostream>
#include <optional>
#include <string>

// Laying a document out, and the three things that read the result: scrolling
// it, selecting across it, and transforming a block of it. They share a lane
// file because they share the fixture note and all four are `doc::Layout`
// directly, with no shell above it.
namespace micronotes::perfharness {

// Budget from the design: one keystroke in a 200 KB note re-lays out in ~2 ms.
static constexpr std::uint64_t kKeystrokeBudgetMicros = 2000;

bool layoutBudgets(std::string* out) {
  std::string source;
  int section = 0;
  while(source.size() < 200 * 1024) {
    source += "## Section " + std::to_string(section) + "\n\n";
    source += "A paragraph with **strong text**, *emphasis*, `code`, a [link](note-" +
              std::to_string(section) + ".md) and enough words to wrap more than once on screen.\n\n";
    source += "- bullet " + std::to_string(section) + "\n- [ ] task " + std::to_string(section) + "\n\n";
    source += "> quoted line " + std::to_string(section) + "\n\n";
    ++section;
  }

  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  {
    micronotes::perf::ScopeTimer timer("layout.initial_200kb");
    layout.update(source, options);
  }
  std::cout << "layout.blocks: " << layout.blockCount() << "\n";

  // Type into a paragraph halfway down and re-lay out, the way a keystroke does.
  std::size_t caret = source.find("paragraph", source.size() / 2);
  if(caret == std::string::npos) caret = source.size() / 2;
  bool ok = true;
  std::vector<std::uint64_t> samples;
  for(int i = 0; i < 24; ++i) {
    source.insert(caret + static_cast<std::size_t>(i), 1, 'x');
    samples.push_back(timeMicros([&] { layout.update(source, options); }));
  }
  std::sort(samples.begin(), samples.end());
  const std::uint64_t median = samples[samples.size() / 2];
  const std::uint64_t worst = samples.back();
  recordMicros("layout.keystroke_relayout_median", median);
  recordMicros("layout.keystroke_relayout_worst", worst);
  std::cout << "layout.keystroke_relaid_blocks: " << layout.lastRelaidBlocks() << "\n";

  if(out) *out = source;
  if(median > kKeystrokeBudgetMicros) {
    std::cerr << "BUDGET FAILED: layout.keystroke_relayout_median " << median
              << "us exceeds " << kKeystrokeBudgetMicros << "us\n";
    ok = false;
  }
  return ok;
}

// What a scroll costs.
//
// This is the scenario the app was missing, and the reason it was missing is
// instructive: every existing budget here measures an *edit*, so the harness
// could be green while the most common interaction in the app -- moving the
// viewport over a note nobody is typing into -- was the slowest thing it did.
// A scroll changes no bytes, so a frame of it should cost approximately
// nothing: the layout is already built, the viewport moved, and the only work
// that has to happen is drawing the rows that came into view.
//
// The budget is per frame, not per gesture. At 60 Hz a frame has 16.7 ms for
// everything -- layout, decoration, text, present -- so a layout pass that eats
// 2 ms of it is already a quarter of the budget for one of the several passes a
// frame makes.
static constexpr std::uint64_t kScrollFrameBudgetMicros = 2000;

bool scrollBudgets(const std::string& source) {
  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);

  // 120 frames: two seconds of a scroll, which is what the running app's rolling
  // frame window reports on.
  constexpr int kFrames = 120;
  const auto before = microcore::perf::captureCounters();
  std::vector<std::uint64_t> samples;
  samples.reserve(kFrames);
  for(int frame = 0; frame < kFrames; ++frame) {
    // Exactly what a wheel event does: nothing to the buffer, nothing to the
    // caret, nothing to the geometry. Only the scroll offset moved, and the
    // scroll offset is not an input to the layout at all.
    samples.push_back(timeMicros([&] { layout.update(source, options); }));
  }
  const auto after = microcore::perf::captureCounters();
  std::sort(samples.begin(), samples.end());
  const std::uint64_t median = samples[samples.size() / 2];
  const std::uint64_t worst = samples.back();
  recordMicros("scroll.frame_relayout_median", median);
  recordMicros("scroll.frame_relayout_worst", worst);

  // The counters are what turn a slow number into a diagnosis: a frame that
  // relaid zero blocks and still took milliseconds spent them re-deriving an
  // answer it already had.
  std::cout << "\n=== what " << kFrames << " scrolled frames did ===\n";
  for(const auto& [name, delta] : microcore::perf::nonZeroCounterDelta(before, after)) {
    std::printf("%-44.*s %12llu  (%.1f per frame)\n", static_cast<int>(name.size()), name.data(),
                static_cast<unsigned long long>(delta),
                static_cast<double>(delta) / static_cast<double>(kFrames));
  }

  if(median <= kScrollFrameBudgetMicros) return true;
  std::cerr << "BUDGET FAILED: scroll.frame_relayout_median " << median << "us exceeds "
            << kScrollFrameBudgetMicros << "us -- a frame that changed nothing is re-laying out "
            << "the whole note\n";
  return false;
}

// A selection reaching the whole note must still cost the window. Ctrl+A on a
// 200 KB note selects 6,600 visual rows and a screen shows forty of them; the
// rects for the other 6,560 are built and discarded, twice per frame -- once to
// paint the selection, once to place the formatting toolbar above it. The band
// is what keeps this off the frame, and this is what says the band is still
// there.
static constexpr std::uint64_t kSelectAllBudgetMicros = 60;

bool selectionBudgets(const std::string& source) {
  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);

  // A window's worth, scrolled to the middle of the note, which is where a band
  // that has stopped working looks most like one that is.
  const float bandTop = layout.totalHeight() / 2.0f;
  const float bandBottom = bandTop + 900.0f;
  std::vector<micronotes::doc::Rect> rects;
  // A local median, like `editBudgets` below: `measureIterations` is defined
  // further down the file and reports allocations these two do not have.
  const auto median = [](const char* name, auto&& body) {
    std::vector<std::uint64_t> samples;
    for(int i = 0; i < 64; ++i) samples.push_back(timeMicros(body));
    std::sort(samples.begin(), samples.end());
    const std::uint64_t value = samples[samples.size() / 2];
    recordMicros(name, value);
    return value;
  };
  const std::uint64_t painted = median("select_all.rects", [&] {
    layout.selectionRectsInto(0, source.size(), bandTop, bandBottom, &rects);
  });
  const std::uint64_t anchored = median("select_all.toolbar_anchor", [&] {
    (void)layout.selectionEnds(0, source.size());
  });
  std::cout << "select_all.rects_in_band: " << rects.size() << " of "
            << layout.selectionRects(0, source.size()).size() << " in the document\n";

  const std::uint64_t worst = std::max(painted, anchored);
  if(worst <= kSelectAllBudgetMicros) return true;
  std::cerr << "BUDGET FAILED: select_all took " << worst << "us, over "
            << kSelectAllBudgetMicros << "us -- a selection is costing the document rather "
            << "than the window\n";
  return false;
}

// Enter, Tab and Backspace each need the note's block partition to find the
// block they act on. That is once per structural key, not once per character, so
// the budget is looser than the keystroke one - but it still has to stay off the
// critical path.
static constexpr std::uint64_t kTransformBudgetMicros = 4000;

// What the app actually pays, now that the reading page lends its partition rather
// than each edit deriving one. Far tighter than the budget above, because a
// borrowed partition leaves an edit with no pass over the document in it at all.
static constexpr std::uint64_t kLentTransformBudgetMicros = 40;

bool editBudgets(const std::string& source) {
  const std::size_t caret = std::min(source.find("bullet", source.size() / 2) + 6, source.size());
  const auto time = [&](const char* name, auto&& transform) {
    std::vector<std::uint64_t> samples;
    for(int i = 0; i < 16; ++i) {
      std::optional<decltype(transform())> edit;
      samples.push_back(timeMicros([&] { edit = transform(); }));
      if(!edit->valid && i == 0) std::cout << name << ": (declined)\n";
    }
    std::sort(samples.begin(), samples.end());
    const std::uint64_t median = samples[samples.size() / 2];
    recordMicros(name, median);
    return median;
  };

  std::uint64_t worst = 0;
  worst = std::max(worst, time("edits.continue_list_200kb",
                               [&] { return micronotes::doc::continueList(source, caret); }));
  worst = std::max(worst, time("edits.outdent_or_unwrap_200kb",
                               [&] { return micronotes::doc::outdentOrUnwrap(source, caret); }));
  worst = std::max(worst, time("edits.toggle_todo_200kb",
                               [&] { return micronotes::doc::toggleTodo(source, caret); }));
  // The typing shortcut runs on every space, so it must reject without scanning.
  worst = std::max(worst, time("edits.typing_shortcut_reject_200kb",
                               [&] { return micronotes::doc::applyMarkdownShortcut(source, caret); }));
  // The block affordances: a drag drop, a multi-block turn-into, and the insert
  // button. Each runs once per gesture, never per keystroke.
  worst = std::max(worst, time("edits.move_blocks_to_200kb",
                               [&] { return micronotes::doc::moveBlocksTo(source, caret, caret, 0); }));
  worst = std::max(worst, time("edits.turn_blocks_into_200kb", [&] {
    return micronotes::doc::turnBlocksInto(source, caret, caret + 400, micronotes::doc::BlockKind::Bullet);
  }));
  worst = std::max(worst, time("edits.insert_block_after_200kb", [&] {
    return micronotes::doc::insertBlockAfter(source, caret, micronotes::doc::BlockKind::Todo);
  }));
  if(worst > kTransformBudgetMicros) {
    std::cerr << "BUDGET FAILED: a block transform took " << worst
              << "us, over " << kTransformBudgetMicros << "us\n";
    return false;
  }

  // The same transforms with the partition handed in, which is the path the app
  // takes: `doc::DocumentLayout` keeps one and `app::editorBlocks` lends it. The
  // measurements above are the fallback for a caller that has none, and the gap
  // between the two lanes is what the lending is worth. Scanned once here for
  // all of them, exactly as the layout holds one across a whole editing session.
  const auto blocks = micronotes::doc::scanBlocks(source);
  std::uint64_t lentWorst = 0;
  lentWorst = std::max(lentWorst, time("edits.continue_list_lent_200kb", [&] {
    return micronotes::doc::continueList(source, caret, blocks);
  }));
  lentWorst = std::max(lentWorst, time("edits.outdent_or_unwrap_lent_200kb", [&] {
    return micronotes::doc::outdentOrUnwrap(source, caret, blocks);
  }));
  lentWorst = std::max(lentWorst, time("edits.toggle_todo_lent_200kb", [&] {
    return micronotes::doc::toggleTodo(source, caret, blocks);
  }));
  lentWorst = std::max(lentWorst, time("edits.insert_block_after_lent_200kb", [&] {
    return micronotes::doc::insertBlockAfter(source, caret, micronotes::doc::BlockKind::Todo, 1, blocks);
  }));
  lentWorst = std::max(lentWorst, time("edits.move_blocks_to_lent_200kb", [&] {
    return micronotes::doc::moveBlocksTo(source, caret, caret, 0, blocks);
  }));
  if(lentWorst > kLentTransformBudgetMicros) {
    std::cerr << "BUDGET FAILED: a block transform with the partition lent took " << lentWorst
              << "us, over " << kLentTransformBudgetMicros << "us -- an edit that is handed the "
              << "blocks must not be walking the document\n";
    return false;
  }
  return true;
}

}
