#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/perf/TraceChannel.h"
#include "core/platform/DurableFile.h"

#include "app/PageView.h"
#include "doc/Edits.h"
#include "doc/Fold.h"
#include "doc/Layout.h"
#include "core/editor/MarkdownEditor.h"
#include "core/markdown/MarkdownParser.h"
#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/FoldState.h"

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <new>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>


// Allocation counting, for the half of a measurement that does not move with the
// machine.
//
// Wall time on a shared developer box swings by a factor of three between runs,
// which is enough to hide a real regression and enough to invent one. An
// allocation count does not: the same binary over the same fixture produces the
// same number on a busy laptop and an idle one. It is also the number that
// matters for two of the three priorities here -- a layout pass that allocates
// once per visual line is burning CPU and memory whatever the clock says.
//
// Thread-local rather than atomic: the harness is single threaded, so this is a
// plain increment on the hottest path in the process rather than a
// read-modify-write, and a background thread could never charge its churn to a
// measured iteration.
namespace {
thread_local std::uint64_t tAllocations = 0;
thread_local std::uint64_t tAllocatedBytes = 0;
// The biggest single allocation, which the count and the total together cannot
// answer: "one big buffer or a thousand small ones" is a different bug each way,
// and a scenario whose byte total is ten times its neighbour's at the same
// allocation count is one buffer being resized, not more work being done.
thread_local std::uint64_t tLargestAllocation = 0;
}

void* operator new(std::size_t size) {
  ++tAllocations;
  tAllocatedBytes += size;
  if(size > tLargestAllocation) tLargestAllocation = size;
  void* memory = std::malloc(size != 0 ? size : 1);
  if(memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  ++tAllocations;
  tAllocatedBytes += size;
  if(size > tLargestAllocation) tLargestAllocation = size;
  void* memory = std::aligned_alloc(static_cast<std::size_t>(alignment),
                                    ((size + static_cast<std::size_t>(alignment) - 1) /
                                     static_cast<std::size_t>(alignment)) *
                                      static_cast<std::size_t>(alignment));
  if(memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }

namespace {

static std::string heavyMarkdown(int seed, int sections) {
  std::ostringstream out;
  out << "# Perf Note " << seed << "\n\n";
  for(int section = 0; section < sections; ++section) {
    out << "## Section " << section << "\n\n";
    out << "This paragraph has searchable text, [a local link](note-" << section << ".md), ";
    out << "[a remote link](https://example.com/" << seed << "/" << section << "), ";
    out << "inline `code`, **strong text**, and raw https://example.com/raw/" << seed << "/" << section << ".\n\n";
    out << "![image " << section << "](.micronotes/attachments/perf-" << seed << "/image-" << section << ".png)\n\n";
    out << "- [x] completed item " << section << "\n";
    out << "- [ ] pending item " << section << "\n";
    out << "  - nested item with more searchable text\n\n";
    out << "| Left | Center | Right |\n|:-----|:------:|------:|\n";
    out << "| alpha " << section << " | beta " << section << " | gamma " << section << " |\n\n";
    out << "> [!NOTE]\n> A callout with enough text to exercise wrapping and inline spans.\n\n";
  }
  return out.str();
}

// The live surface has no fonts in the core library, so the budget runs against
// a fixed-advance stand-in. It exercises the same scan, cache and flow work.
static micronotes::doc::Metrics stubMetrics() {
  micronotes::doc::Metrics metrics;
  metrics.measure = [](std::string_view value, const micronotes::doc::RunStyle& style) {
    std::size_t glyphs = 0;
    for(const char c : value) {
      if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
    }
    return static_cast<float>(glyphs) * style.size * 0.6f;
  };
  metrics.lineHeight = [](const micronotes::doc::RunStyle& style) { return std::round(style.size * 1.5f); };
  return metrics;
}

// The harness measures whole operations itself and hands the medians to the
// same ranked table the scopes feed, so one table answers "what is slow" for
// both. The channel speaks milliseconds; the budgets below are in microseconds,
// which is the resolution a 2 ms budget needs.
static void recordMicros(std::string_view name, std::uint64_t micros) {
  micronotes::perf::traceChannel().recordSample(name, static_cast<double>(micros) / 1000.0);
}

// Process CPU time, not wall clock.
//
// Wall clock includes every microsecond this process spent *descheduled*, which
// on a machine that is doing anything else is most of the number: the same
// keystroke measured 292 us at load 1 and 898 us at load 11 on this box, and
// neither figure was about the code. Nothing here waits on IO or on another
// thread, so CPU time is the operation's actual cost and it is what a budget
// can be written against on a shared machine. The clock is a syscall rather
// than a vDSO read, so it costs a few hundred nanoseconds -- irrelevant against
// the tens-to-thousands of microseconds every scenario below measures, and a
// far smaller error than the scheduler.
static std::uint64_t cpuMicros() {
  timespec now {};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec) / 1000ull;
}

// One timed call. Every budget in this file used to spell the clock out for
// itself, five copies of the same duration_cast -- which is also why switching
// the clock was a five-place edit rather than a one-place one.
template <typename Fn>
static std::uint64_t timeMicros(Fn&& body) {
  const std::uint64_t start = cpuMicros();
  body();
  return cpuMicros() - start;
}

// Budget from the design: one keystroke in a 200 KB note re-lays out in ~2 ms.
static constexpr std::uint64_t kKeystrokeBudgetMicros = 2000;

static bool layoutBudgets(std::string* out) {
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
    options.caretOffset = caret;
    samples.push_back(timeMicros([&] { layout.update(source, options); }));
  }
  std::sort(samples.begin(), samples.end());
  const std::uint64_t median = samples[samples.size() / 2];
  const std::uint64_t worst = samples.back();
  recordMicros("layout.keystroke_relayout_median", median);
  recordMicros("layout.keystroke_relayout_worst", worst);
  std::cout << "layout.keystroke_relaid_blocks: " << layout.lastRelaidBlocks() << "\n";

  // Folding puts a predicate on every block of every relayout, so it belongs
  // under the same keystroke budget as the layout it runs inside.
  {
    micronotes::ui::FoldState folds;
    folds.toggle("perf", micronotes::doc::foldKey(source, micronotes::doc::scanBlocks(source).front()));
    options.folded = [&folds, &source](const micronotes::doc::SourceBlock& block) {
      return folds.folded("perf", micronotes::doc::foldKey(source, block));
    };
    std::vector<std::uint64_t> folded;
    for(int i = 0; i < 12; ++i) {
      source.insert(caret + static_cast<std::size_t>(i), 1, 'y');
      options.caretOffset = caret;
      folded.push_back(timeMicros([&] { layout.update(source, options); }));
    }
    std::sort(folded.begin(), folded.end());
    const std::uint64_t foldedMedian = folded[folded.size() / 2];
    recordMicros("layout.keystroke_relayout_folded_median", foldedMedian);
    options.folded = nullptr;
    if(foldedMedian > kKeystrokeBudgetMicros) {
      std::cerr << "BUDGET FAILED: layout.keystroke_relayout_folded_median " << foldedMedian
                << "us exceeds " << kKeystrokeBudgetMicros << "us\n";
      ok = false;
    }
  }
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

static bool scrollBudgets(const std::string& source) {
  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  options.caretOffset = 0;
  layout.update(source, options);

  // 120 frames: two seconds of a scroll, which is what the live app's rolling
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

static bool selectionBudgets(const std::string& source) {
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

// What the app actually pays, now that the live page lends its partition rather
// than each edit deriving one. Far tighter than the budget above, because a
// borrowed partition leaves an edit with no pass over the document in it at all.
static constexpr std::uint64_t kLentTransformBudgetMicros = 40;

static bool editBudgets(const std::string& source) {
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

// ---------------------------------------------------------------------------
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

struct Cost {
  std::uint64_t medianMicros = 0;
  std::uint64_t worstMicros = 0;
  double allocations = 0.0;
  double kilobytes = 0.0;
  std::uint64_t largestBytes = 0;
};

template <typename Fn>
static Cost measureIterations(const char* name, int count, Fn&& iteration) {
  std::vector<std::uint64_t> samples;
  samples.reserve(static_cast<std::size_t>(count));
  // One untimed pass first. Without it the first scenario in the file absorbs
  // every buffer the layout grows once and keeps -- the block array, the source
  // ping-pong, the reuse map -- and reports them as the cost of a keystroke:
  // typing near the top read 189 KB per keystroke against 9 KB in the middle,
  // and the whole difference was the warm-up, not the position. Every scenario
  // here is meant to measure a steady state, and this is what puts it in one.
  iteration(0);
  const std::uint64_t allocationsBefore = tAllocations;
  const std::uint64_t bytesBefore = tAllocatedBytes;
  tLargestAllocation = 0;
  for(int i = 0; i < count; ++i) {
    samples.push_back(timeMicros([&] { iteration(i); }));
  }
  const double runs = static_cast<double>(count);
  Cost cost;
  cost.allocations = static_cast<double>(tAllocations - allocationsBefore) / runs;
  cost.kilobytes = static_cast<double>(tAllocatedBytes - bytesBefore) / runs / 1024.0;
  cost.largestBytes = tLargestAllocation;
  std::sort(samples.begin(), samples.end());
  cost.medianMicros = samples[samples.size() / 2];
  cost.worstMicros = samples.back();
  recordMicros(name, cost.medianMicros);
  std::printf("%-40s %7llu us median %7llu us worst %10.0f allocs %10.1f KB %8.1f KB max\n",
              name, static_cast<unsigned long long>(cost.medianMicros),
              static_cast<unsigned long long>(cost.worstMicros), cost.allocations, cost.kilobytes,
              static_cast<double>(cost.largestBytes) / 1024.0);
  return cost;
}

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

static bool interactionBudgets(const std::string& base) {
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

// ---------------------------------------------------------------------------
// The persistence lane.
//
// Autosave runs 1.2 s after the last keystroke and again every second while
// typing continues, and until this lane existed nothing measured it. Every
// other budget in this file is a layout or a paint, so a save could grow a
// whole-library tree walk and a whole-file read and the harness would stay
// green -- which is exactly what had happened.
//
// Timed on the **wall clock**, not on CLOCK_PROCESS_CPUTIME_ID like every
// scenario above. A durable write is two fsync barriers, and a barrier is time
// the process spends blocked rather than running: measured on CPU time the
// 1.1 ms this costs per save reads as about 60 us of work, which is a
// measurement of the wrong thing. The allocation counts beside the medians are
// still the machine-independent half.
static std::uint64_t wallMicros() {
  timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec) / 1000ull;
}

template <typename Fn>
static Cost measureWallIterations(const char* name, int count, Fn&& iteration) {
  std::vector<std::uint64_t> samples;
  samples.reserve(static_cast<std::size_t>(count));
  iteration(0);
  const std::uint64_t allocationsBefore = tAllocations;
  const std::uint64_t bytesBefore = tAllocatedBytes;
  tLargestAllocation = 0;
  for(int i = 0; i < count; ++i) {
    const std::uint64_t start = wallMicros();
    iteration(i);
    samples.push_back(wallMicros() - start);
  }
  const double runs = static_cast<double>(count);
  Cost cost;
  cost.allocations = static_cast<double>(tAllocations - allocationsBefore) / runs;
  cost.kilobytes = static_cast<double>(tAllocatedBytes - bytesBefore) / runs / 1024.0;
  cost.largestBytes = tLargestAllocation;
  std::sort(samples.begin(), samples.end());
  cost.medianMicros = samples[samples.size() / 2];
  cost.worstMicros = samples.back();
  recordMicros(name, cost.medianMicros);
  std::printf("%-40s %7llu us median %7llu us worst %10.0f allocs %10.1f KB %8.1f KB max\n",
              name, static_cast<unsigned long long>(cost.medianMicros),
              static_cast<unsigned long long>(cost.worstMicros), cost.allocations, cost.kilobytes,
              static_cast<double>(cost.largestBytes) / 1024.0);
  return cost;
}

// One autosave of a 200 KB note in a 1,000-note library. Loose, and wall-clock:
// the floor is whatever this filesystem charges for two fsyncs, which is not
// something the code can be held to. It is here to catch a save that acquires
// a second whole-library pass, not to police the disk.
static constexpr std::uint64_t kAutosaveBudgetMicros = 12000;
// Posting the recovery copy is what a *keystroke* pays. It is a copy into a
// mailbox and a notify, so it belongs under the keystroke budget with the
// layout -- and well under it, since the layout has to fit there too.
static constexpr std::uint64_t kRecoveryPostBudgetMicros = 400;

static bool persistenceBudgets(const std::filesystem::path& root, const std::string& body) {
  std::cout << "\n=== what saving a note costs ===\n";
  micronotes::ui::AppState state;
  if(!state.openOrCreateLibrary(root)) {
    std::cerr << "persistence lane: could not open the fixture library\n";
    return false;
  }
  const auto notes = state.allNotes();
  if(notes.empty()) {
    std::cerr << "persistence lane: the fixture library has no notes\n";
    return false;
  }
  state.selectNote(notes.front().id);

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  std::string text = body;
  gate("save.autosave_note", measureWallIterations("save.autosave_note", 16, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)state.saveSelectedNote(text);
       }),
       kAutosaveBudgetMicros);

  // The UI thread's half of recovery: the queue absorbs the disk, so this is a
  // copy and a notify however busy the writer is.
  gate("save.recovery_post", measureWallIterations("save.recovery_post", 200, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)state.saveSelectedNoteRecovery(text);
       }),
       kRecoveryPostBudgetMicros);
  (void)state.clearSelectedNoteRecovery();

  // A save with nothing else open, so the number is the write and the index
  // update rather than the note list rebuild the two above also pay for.
  gate("save.durable_write_200kb", measureWallIterations("save.durable_write_200kb", 16, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)microcore::platform::writeFileDurably(root / ".micronotes" / "perf-write.tmp", text);
       }),
       kAutosaveBudgetMicros);
  std::filesystem::remove(root / ".micronotes" / "perf-write.tmp");
  return ok;
}

// ---------------------------------------------------------------------------
// The undo lane.
//
// Every other lane in this file measures a *rate* -- what a keystroke, a frame
// or a save costs. Undo is not a rate: it is a **ceiling**, a thing the editor
// holds on to for as long as the note is open, and the only number in this file
// shaped like it is `peak_rss` at the very bottom, which covers the whole
// process and so cannot attribute anything to the editor.
//
// That is why nothing here saw an undo history costing a hundred times the note
// it belonged to. `editor_undo_history_is_bounded` asserted a byte figure and
// drove it with a 4 KB buffer, so it measured 400 KB against a 32 MB assertion;
// the harness measured nothing at all. A ceiling needs a lane that reports the
// ceiling, driven at a size where the ceiling is what runs out.
//
// So this lane reports **bytes retained**, next to the size of the note they
// belong to, which is the comparison that makes the number mean something: a
// history that costs a multiple of the note is a history storing the note.

// What a session's undo history may retain, as a multiple of the note.
//
// The floor under any scheme is one record per step -- a few dozen bytes of
// offsets and caret positions -- so on a small note the interesting question is
// whether the *note* is in there. 32 KB over a 2 KB note is 16x, which a splice
// history clears by a wide margin and a snapshot history cannot come close to.
static constexpr std::size_t kUndoSmallNoteBytes = 32u * 1024u;
// The same session on a 200 KB note. Retention that does not grow with the
// note's size is the whole point: this budget is the *same* order as the one
// above even though the note is a hundred times bigger.
static constexpr std::size_t kUndoLargeNoteBytes = 64u * 1024u;
// Deleting text is the one case a history has to keep bytes: what came out is
// the only copy left. Forty whole-note replacements of a 200 KB note is 8 MB of
// genuinely irreducible history, and this is the ceiling that has to bite.
static constexpr std::size_t kUndoReplacementBytes = 9u * 1024u * 1024u;
// One Ctrl+Z on a 200 KB note. It used to be a whole-buffer copy onto the redo
// stack plus a whole-buffer word recount; it is a splice now, and the budget is
// the keystroke's.
static constexpr std::uint64_t kUndoStepBudgetMicros = 2000;

namespace {

struct UndoSession {
  std::size_t retainedBytes = 0;
  std::size_t depth = 0;
};

// A session: `edits` deliberate edits, each sealed off from the last by a caret
// move, which is what a person does when they fix a word here and a word there.
// Sealed rather than coalesced on purpose -- a coalesced run is one record and
// would measure the coalescing rather than the retention.
static UndoSession undoSession(std::size_t noteBytes, int edits) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(noteBytes, 'x'));
  for(int i = 0; i < edits; ++i) {
    editor.moveCursor(static_cast<std::size_t>(i) % (noteBytes + 1));
    editor.insert("a");
  }
  return {editor.undoBytes(), editor.undoDepth()};
}

}

static bool undoBudgets() {
  std::cout << "\n=== what an editing session retains for undo ===\n";
  bool ok = true;
  const auto report = [&](const char* name, const UndoSession& session, std::size_t noteBytes,
                          std::size_t budget) {
    std::printf("%-40s %10.1f KB retained %6zu steps %8.1f KB note %6.2fx\n", name,
                static_cast<double>(session.retainedBytes) / 1024.0, session.depth,
                static_cast<double>(noteBytes) / 1024.0,
                static_cast<double>(session.retainedBytes) / static_cast<double>(noteBytes));
    if(session.retainedBytes <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " retains " << session.retainedBytes
              << " bytes, over " << budget << "\n";
    ok = false;
  };

  // A small note, edited for a long time. This is the case the count ceiling
  // was written for and the one it governs, so what it retains is the depth
  // times whatever a step costs -- which is the whole question.
  report("undo.session_2kb_note", undoSession(2u * 1024u, 200), 2u * 1024u, kUndoSmallNoteBytes);
  // The same session, a hundred times the note.
  report("undo.session_200kb_note", undoSession(200u * 1024u, 400), 200u * 1024u,
         kUndoLargeNoteBytes);

  // Whole-note replacements: the deleted bytes are real history and cannot be
  // dropped, so this is the scenario the byte ceiling exists for. It must stay
  // bounded, and it must still be an undo history at the end of it.
  {
    const std::size_t noteBytes = 200u * 1024u;
    microcore::editor::MarkdownEditor editor;
    editor.setText(std::string(noteBytes, 'x'));
    for(int i = 0; i < 80; ++i) {
      editor.selectAll();
      editor.insert(std::string(noteBytes, static_cast<char>('a' + (i % 26))));
    }
    report("undo.whole_note_replacements", {editor.undoBytes(), editor.undoDepth()}, noteBytes,
           kUndoReplacementBytes);
    if(editor.undoDepth() < 8) {
      std::cerr << "BUDGET FAILED: undo.whole_note_replacements kept only " << editor.undoDepth()
                << " steps\n";
      ok = false;
    }
  }

  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // And what one step *costs*, which is the other half of the story: a history
  // of whole-buffer snapshots is not only large, it charges a copy of the note
  // and a full word recount for every Ctrl+Z.
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText(std::string(200u * 1024u, 'x'));
    for(int i = 0; i < 64; ++i) {
      editor.moveCursor(static_cast<std::size_t>(i) * 512u);
      editor.insert("a");
    }
    gate("undo.step_200kb_note", measureIterations("undo.step_200kb_note", 32, [&](int) {
           if(!editor.undo()) editor.redo();
         }),
         kUndoStepBudgetMicros);
    while(editor.redo()) {
    }
    gate("redo.step_200kb_note", measureIterations("redo.step_200kb_note", 32, [&](int) {
           if(!editor.undo()) editor.redo();
         }),
         kUndoStepBudgetMicros);
  }
  return ok;
}

// ---------------------------------------------------------------------------
// The search lane.
//
// Search had no budget at all, which made it the same blind spot autosave was
// before the eighth pass: it runs on *every keystroke* in the search box, over
// the whole library, and every instrument here measured a layout or a save.
//
// The two scenarios are the two different queries, and they fail differently.
// A query that matches goes through fts5 and then builds a snippet per result;
// a query that matches nothing falls through fts to the `LIKE` scan, which
// reads `lower(body)` of every note in the library and is the worst case by a
// wide margin. Typing a word one letter at a time passes through several of the
// second kind before reaching the first.
//
// Wall clock, like the persistence lane: this one goes to SQLite, so part of
// the cost is page-cache reads rather than work on this thread.
//
// The allocation column is the number to read first here. It is deterministic,
// and it is what says whether the query is *copying the library* to look at it:
// a result set is capped at 200 rows, so a per-row whole-body copy shows up as
// a fixed several-hundred allocations however small the query is.
static constexpr std::uint64_t kSearchHitBudgetMicros = 25000;
static constexpr std::uint64_t kSearchMissBudgetMicros = 60000;

// Somewhere for a result set to go. Without it the optimiser is entitled to
// notice that nothing reads the vector and delete the query that filled it,
// which is a lane that measures nothing and says it is fast.
static volatile std::size_t sink = 0;

static bool searchBudgets(const std::filesystem::path& root) {
  std::cout << "\n=== what a search costs ===\n";
  micronotes::library::LibraryIndex index;
  if(!index.open(root)) {
    std::cerr << "search lane: could not open the fixture index\n";
    return false;
  }
  if(index.size() == 0) {
    std::cerr << "search lane: the fixture index is empty\n";
    return false;
  }

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Every note in the fixture carries this word, so the result set is the
  // 200-row cap and every one of those rows has a snippet built for it.
  gate("search.query_hits_everything",
       measureWallIterations("search.query_hits_everything", 8, [&](int) {
         const auto results = index.search("searchable", micronotes::library::SearchScope::All);
         sink += results.size();
       }),
       kSearchHitBudgetMicros);

  // No note carries this, so fts5 returns nothing and the query falls through
  // to the LIKE scan over every row. The path a half-typed word takes.
  gate("search.query_matches_nothing",
       measureWallIterations("search.query_matches_nothing", 8, [&](int) {
         const auto results = index.search("zqxjvw", micronotes::library::SearchScope::All);
         sink += results.size();
       }),
       kSearchMissBudgetMicros);

  // Titles only. A narrower column and a much smaller result set, so this is
  // the control: it shares every part of the path except the bodies.
  gate("search.query_titles_only",
       measureWallIterations("search.query_titles_only", 8, [&](int) {
         const auto results = index.search("Perf", micronotes::library::SearchScope::Title);
         sink += results.size();
       }),
       kSearchHitBudgetMicros);
  return ok;
}

// Ranked by self time, because that is the only ordering that answers "what
// should I look at first": an outer scope that merely contains an expensive one
// must not outrank the expensive one. The per-call and max columns separate
// "slow once" from "fast but called far too often" -- two problems with
// different fixes that a single total conflates.
static void printSamples() {
  std::cout << "\n=== scope timings (ranked by self ms) ===\n";
  microcore::perf::traceChannel().write(stdout);
}

// The counters are the other half of the picture: timings say where the time
// went, counters say how many times the work happened at all. A scope that
// looks cheap per call but ran 40,000 times is invisible in the table above.
static void printCounters() {
  std::cout << "\n";
  microcore::perf::writeCounters(stdout);
}

// ---------------------------------------------------------------------------
// The font lane.
//
// Every budget above measures against `stubMetrics()`, a fixed-advance stand-in,
// because the core library has no fonts in it. That is the right call for a
// core-level benchmark and it is also why the largest cost in the app -- glyph
// shaping to open a note -- was invisible here for four passes and only showed
// up in a real session.
//
// It is measurable now because the shell is a library: `documentMetrics` is the
// same measure and line height `PageView` lays a note out with, over the same
// `ui::TextRenderer`, reading the same vendored faces. What is still not here
// is a *draw* -- no window, no textures, no present -- so this lane says what
// shaping and measuring cost and says nothing about rasterizing. That is the
// half that was missing: `render.text_measure_calls` and its hit rate are the
// numbers a layout change moves, and they were unobservable outside a session.
//
// Deliberately loose budgets, and more loosely still than the stubbed ones: a
// shaping pass is the scenario here whose cost moves most with the machine --
// 65 ms on an idle box and 200 ms on one running a parallel build, measured
// with the same binary and the same CPU clock. The point of the lane is to
// catch a change that makes shaping several times worse; the counters beside it
// are the tight numbers, and they are deterministic.
static constexpr std::uint64_t kFontColdBudgetMicros = 60000;
static constexpr std::uint64_t kFontShapingBudgetMicros = 400000;
static constexpr std::uint64_t kFontKeystrokeBudgetMicros = 20000;

// A note whose every word is different.
//
// The 200 KB fixture the other lanes share is written by a loop, so it repeats
// a few thousand distinct words several thousand times -- and a cold open of it
// is 99.2% measure-cache hits, which means it says almost nothing about
// shaping. Real prose is not that repetitive and neither is this: each word is
// a base-36 counter padded to a varied length, so every measurement is a miss
// and the lane measures the face rather than the hash table in front of it.
static std::string uniqueWordMarkdown(std::size_t bytes, int seed) {
  static const char kDigits[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string out;
  out.reserve(bytes + 256);
  std::size_t counter = static_cast<std::size_t>(seed) * 4'000'000ull;
  int section = 0;
  while(out.size() < bytes) {
    out += "## Section ";
    out += std::to_string(section++);
    out += "\n\n";
    for(int line = 0; line < 12; ++line) {
      for(int word = 0; word < 11; ++word) {
        if(word != 0) out.push_back(' ');
        std::size_t value = ++counter;
        // Three to eight characters, so the word lengths vary the way prose
        // does and one cached width cannot stand in for another.
        const int length = 3 + static_cast<int>(counter % 6);
        for(int c = 0; c < length; ++c) {
          out.push_back(kDigits[value % 36]);
          value /= 36;
        }
      }
      out.push_back('\n');
    }
    out += "\n";
  }
  return out;
}

static bool fontBudgets(const std::string& base) {
  micronotes::ui::TextRenderer text(nullptr);
  if(!text.fonts().ready()) {
    std::cout << "\n=== the real font path === (skipped: no usable face on this machine)\n";
    return true;
  }
  std::cout << "\n=== the real font path (" << text.fonts().sourceDescription() << ") ===\n";

  const auto freshMetrics = [&text] {
    auto metrics = micronotes::app::documentMetrics(text);
    // A `Complex` block is drawn by md4c, which this lane does not build. Zero
    // is what an unwired page already answers, so the note lays out with those
    // blocks empty rather than not at all.
    metrics.measureComplex = [](const micronotes::doc::SourceBlock&, float) { return 0.0f; };
    return metrics;
  };

  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  options.type = micronotes::app::documentTypeMetrics();

  bool ok = true;
  const auto gate = [&ok](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Opening the note with a cold measure cache each time -- which is what
  // starting the app does, and the one place shaping is the whole cost.
  const auto before = microcore::perf::captureCounters();
  gate("font.open_cold_layout",
       measureIterations("font.open_cold_layout", 4,
                         [&](int i) {
                           micronotes::doc::DocumentLayout fresh;
                           fresh.setMetrics(freshMetrics());
                           micronotes::doc::LayoutOptions cold = options;
                           cold.width = 700.0f + static_cast<float>(i);
                           fresh.update(base, cold);
                         }),
       kFontColdBudgetMicros);
  const auto afterCold = microcore::perf::captureCounters();

  // The same open over prose the cache cannot absorb. This is the shaping
  // number: every word is measured once and no second word has its width.
  //
  // A different note per pass, because the measure cache belongs to the
  // renderer and outlives the layout: run one note four times and only the
  // first pass shapes anything, which is a 75% hit rate and three quarters of
  // the answer missing. Counted rather than indexed by `i`, because
  // `measureIterations` makes an untimed warm-up call that would otherwise
  // hand the first timed pass a note already in the cache.
  std::vector<std::string> uniqueNotes;
  for(int note = 0; note < 5; ++note) uniqueNotes.push_back(uniqueWordMarkdown(200 * 1024, note));
  std::size_t pass = 0;
  gate("font.open_unique_words",
       measureIterations("font.open_unique_words", 4,
                         [&](int i) {
                           micronotes::doc::DocumentLayout fresh;
                           fresh.setMetrics(freshMetrics());
                           micronotes::doc::LayoutOptions cold = options;
                           cold.width = 700.0f + static_cast<float>(i);
                           fresh.update(uniqueNotes[pass++ % uniqueNotes.size()], cold);
                         }),
       kFontShapingBudgetMicros);
  const auto afterUnique = microcore::perf::captureCounters();

  // And a keystroke into it, where the cache is warm and the measure calls are
  // a hash and a probe. This is the number that says whether a layout change
  // moved the *shaping* or only the arithmetic around it.
  std::string source = base;
  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(freshMetrics());
  layout.update(source, options);
  std::size_t caret = source.find("paragraph", source.size() / 2);
  if(caret == std::string::npos) caret = source.size() / 2;
  gate("font.type_middle", measureIterations("font.type_middle", 24,
                                             [&](int i) {
                                               source.insert(caret + static_cast<std::size_t>(i), 1, 'x');
                                               options.caretOffset = caret;
                                               layout.update(source, options);
                                             }),
       kFontKeystrokeBudgetMicros);
  const auto afterType = microcore::perf::captureCounters();

  // The hit rate is the diagnosis behind both numbers: a cold open is misses,
  // a keystroke should be almost entirely hits, and a change that turns the
  // second into the first is a regression no timing on a loaded machine would
  // have shown reliably.
  const auto rate = [](std::uint64_t calls, std::uint64_t hits) {
    return calls == 0 ? 0.0 : 100.0 * static_cast<double>(hits) / static_cast<double>(calls);
  };
  const auto delta = [](const auto& from, const auto& to, microcore::perf::CounterId id) {
    return to[static_cast<std::size_t>(id)] - from[static_cast<std::size_t>(id)];
  };
  using microcore::perf::CounterId;
  const std::uint64_t coldCalls = delta(before, afterCold, CounterId::RenderTextMeasureCalls);
  const std::uint64_t coldHits = delta(before, afterCold, CounterId::RenderTextMeasureCacheHits);
  const std::uint64_t uniqueCalls = delta(afterCold, afterUnique, CounterId::RenderTextMeasureCalls);
  const std::uint64_t uniqueHits = delta(afterCold, afterUnique, CounterId::RenderTextMeasureCacheHits);
  const std::uint64_t typeCalls = delta(afterUnique, afterType, CounterId::RenderTextMeasureCalls);
  const std::uint64_t typeHits = delta(afterUnique, afterType, CounterId::RenderTextMeasureCacheHits);
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.open_cold_layout.measures",
              static_cast<unsigned long long>(coldCalls), static_cast<unsigned long long>(coldHits),
              rate(coldCalls, coldHits));
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.open_unique_words.measures",
              static_cast<unsigned long long>(uniqueCalls), static_cast<unsigned long long>(uniqueHits),
              rate(uniqueCalls, uniqueHits));
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.type_middle.measures",
              static_cast<unsigned long long>(typeCalls), static_cast<unsigned long long>(typeHits),
              rate(typeCalls, typeHits));
  return ok;
}

}

// Peak resident memory for the whole run, which is the only number here that is
// about the *ceiling* rather than about a rate. The per-scenario columns say how
// much a keystroke allocates and hands straight back; this says how much the
// process was holding at its worst moment, and a layout cache that keeps three
// generations of a document is visible in one and not the other.
//
// It covers the fixture as well as the layout -- a thousand notes on disk and an
// SQLite index are in here too -- so it is a number to watch move between runs
// rather than to attribute to any one part.
static void printPeakMemory() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while(std::getline(status, line)) {
    if(line.compare(0, 6, "VmHWM:") != 0) continue;
    const auto digits = line.find_first_of("0123456789");
    if(digits == std::string::npos) break;
    std::printf("\n=== peak resident memory ===\npeak_rss %.1f MB\n",
                std::strtod(line.c_str() + digits, nullptr) / 1024.0);
    return;
  }
}

int main() {
  // The harness measures whether or not the developer remembered to export
  // anything: a benchmark whose instrumentation is off by default measures
  // nothing. The live app leaves both channels to the environment.
  microcore::perf::markMainThread();
  microcore::perf::traceChannel().setAggregateEnabled(true);

  const auto root = std::filesystem::temp_directory_path() / "micronotes-perf-fixture";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  {
    microcore::perf::ScopeTimer timer("fixture.large_library.create_1000_notes");
    for(int i = 0; i < 1000; ++i) {
      micronotes::library::NoteMetadata metadata;
      metadata.id = "perf-" + std::to_string(i);
      metadata.title = "Perf Note " + std::to_string(i);
      metadata.tags = {"perf", i % 2 == 0 ? "even" : "odd"};
      auto path = library.createNote(metadata, heavyMarkdown(i, 3));
      if(i % 4 == 0) library.moveNote(path, "work");
      else if(i % 4 == 1) library.moveNote(path, "ideas");
    }
  }

  micronotes::library::LibraryIndex index;
  index.open(root);
  index.refreshChangedFiles();
  index.refreshChangedFiles();
  {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "perf-updated";
    metadata.title = "Perf Updated";
    library.createNote(metadata, heavyMarkdown(2000, 8));
    index.refreshChangedFiles();
  }
  (void)index.search("searchable");

  {
    micronotes::ui::AppState state;
    microcore::perf::ScopeTimer timer("fixture.app_state.open_select_and_list");
    state.openOrCreateLibrary(root);
    state.selectFolder("work");
    (void)state.folders();
    (void)state.tags();
    auto notes = state.currentNotes();
    if(!notes.empty()) {
      state.selectNote(notes.front().id);
      (void)state.readSelectedNote();
    }
  }

  {
    microcore::markdown::MarkdownParser parser;
    microcore::perf::ScopeTimer timer("fixture.markdown.parse_heavy_document");
    const auto doc = parser.parse(heavyMarkdown(9999, 200));
    std::cout << "heavy_document.blocks: " << doc.blocks.size() << "\n";
  }

  std::string liveNote;
  bool withinBudget = layoutBudgets(&liveNote);
  withinBudget = editBudgets(liveNote) && withinBudget;
  withinBudget = scrollBudgets(liveNote) && withinBudget;
  withinBudget = selectionBudgets(liveNote) && withinBudget;
  withinBudget = interactionBudgets(liveNote) && withinBudget;
  withinBudget = fontBudgets(liveNote) && withinBudget;
  withinBudget = persistenceBudgets(root, liveNote) && withinBudget;
  withinBudget = searchBudgets(root) && withinBudget;
  withinBudget = undoBudgets() && withinBudget;

  printSamples();
  printCounters();
  printPeakMemory();
  std::filesystem::remove_all(root);
  return withinBudget ? 0 : 1;
}
