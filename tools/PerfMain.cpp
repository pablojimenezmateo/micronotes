#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/perf/TraceChannel.h"

#include "doc/Edits.h"
#include "doc/Fold.h"
#include "doc/Layout.h"
#include "core/markdown/MarkdownParser.h"
#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "ui/AppState.h"
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

// Enter, Tab and Backspace each rescan the note to find the block they act on.
// That is once per structural key, not once per character, so the budget is
// looser than the keystroke one - but it still has to stay off the critical path.
static constexpr std::uint64_t kTransformBudgetMicros = 4000;

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
      (void)state.selectedNote();
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
  withinBudget = interactionBudgets(liveNote) && withinBudget;

  printSamples();
  printCounters();
  printPeakMemory();
  std::filesystem::remove_all(root);
  return withinBudget ? 0 : 1;
}
