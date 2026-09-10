#include "TestSupport.h"
#include "LayoutFixture.h"

#include "core/perf/PerformanceCounters.h"
#include "doc/Layout.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

using micronotes::doc::BlockKind;
using micronotes::doc::DocumentLayout;
using micronotes::doc::LayoutOptions;
using micronotes::doc::Metrics;
using micronotes::doc::Rect;
using micronotes::doc::RunStyle;
using micronotes::tests::isSpace;
using micronotes::tests::kFixture;
using micronotes::tests::nextBoundary;
using micronotes::tests::stubMeasure;
using micronotes::tests::stubMetrics;
using micronotes::tests::caretRectByWalking;
using micronotes::tests::counter;
using micronotes::tests::layoutsAgree;
using micronotes::tests::longFixture;
using micronotes::tests::manyBlocks;
using micronotes::tests::oneHugeFence;
using micronotes::tests::sectionedFixture;
using micronotes::tests::walkRandomEdits;

// The incremental update: what a keystroke rebuilds, what it reuses, and what a
// fold or a changed input forces. `doc/LayoutUpdate.cpp` is the unit.
//
// The two `..._match_a_layout_built_from_scratch` cases are the ones that matter
// most: every other test here says the update did less work, and only those say
// it got the same answer.

MICRONOTES_TEST(layout_reuses_cached_blocks_across_a_keystroke) {
  std::string source;
  for(int i = 0; i < 400; ++i) source += "Paragraph number " + std::to_string(i) + " with some words.\n\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 400.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.blockCount() > 700);
  MICRONOTES_REQUIRE(layout.lastRelaidBlocks() > 300);

  source.insert(source.find("number 3") + 6, "X");
  layout.update(source, options);
  micronotes::tests::require(layout.lastRelaidBlocks() <= 2,
                             "a keystroke re-laid " + std::to_string(layout.lastRelaidBlocks()) + " blocks");
}

// The page re-lays the note out once per frame whether or not anything
// happened, so a scroll is a frame with nothing to do. It used to copy, rescan,
// re-hash and re-flatten the whole note anyway -- about 70% of the frame on a
// 235 KB one, and every bit of it reproducing the layout already in hand.
MICRONOTES_TEST(layout_update_does_nothing_when_nothing_changed) {
  const std::string source = manyBlocks(200);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);

  const auto before = microcore::perf::captureCounters();
  for(int frame = 0; frame < 10; ++frame) layout.update(source, options);
  const auto after = microcore::perf::captureCounters();

  using microcore::perf::CounterId;
  const auto delta = [&](CounterId id) {
    return after[static_cast<std::size_t>(id)] - before[static_cast<std::size_t>(id)];
  };
  MICRONOTES_REQUIRE(delta(CounterId::LayoutUpdateCalls) == 10);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutUnchangedUpdates) == 10);
  // Not one byte copied, not one block rescanned or rehashed, not one block
  // walked. These are the four O(document) passes the fast path exists to skip,
  // and each of them reading zero is the whole claim.
  MICRONOTES_REQUIRE(delta(CounterId::LayoutSourceBytesCopied) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutKeyBytesHashed) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutBlocksScanned) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutBlocksWalked) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutBlocksRescanned) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutBlocksShifted) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutSourceBytesMoved) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutVisualRows) == 0);
}

// Every input the reuse check covers has to actually invalidate, or the fast
// path is a correctness bug that only shows up as a stale screen.
MICRONOTES_TEST(layout_update_rebuilds_when_an_input_moves) {
  const std::string source = manyBlocks(20);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;

  const auto rebuilds = [&](const LayoutOptions& next, std::string_view text) {
    const auto before = counter(microcore::perf::CounterId::LayoutUnchangedUpdates);
    layout.update(text, next);
    return counter(microcore::perf::CounterId::LayoutUnchangedUpdates) == before;
  };

  layout.update(source, options);
  MICRONOTES_REQUIRE(!rebuilds(options, source));  // unchanged: reused

  LayoutOptions narrower = options;
  narrower.width = 500.0f;
  MICRONOTES_REQUIRE(rebuilds(narrower, source));

  const std::string edited = source + "\nA new paragraph.\n";
  MICRONOTES_REQUIRE(rebuilds(narrower, edited));

  // Same length, different bytes: a length check alone would miss this, and the
  // screen would keep the old text.
  std::string swapped = edited;
  swapped[swapped.size() / 2] = swapped[swapped.size() / 2] == 'x' ? 'y' : 'x';
  MICRONOTES_REQUIRE(rebuilds(narrower, swapped));

  // New faces mean every cached block was measured with the wrong ones, and
  // neither the source nor the geometry says so.
  layout.update(swapped, narrower);
  layout.setMetrics(stubMetrics());
  MICRONOTES_REQUIRE(rebuilds(narrower, swapped));
}

// A caller with no stamp to offer must get exactly the behaviour it had before
// the stamps existed: zero means "cannot say", not "nothing changed".
MICRONOTES_TEST(layout_an_unstamped_caller_still_compares_bytes) {
  const std::string source = manyBlocks(8);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);
  const auto before = counter(microcore::perf::CounterId::LayoutUnchangedUpdates);
  layout.update(source, options);
  MICRONOTES_REQUIRE(counter(microcore::perf::CounterId::LayoutUnchangedUpdates) == before + 1);

  const std::string edited = source + "\nAnother paragraph.\n";
  layout.update(edited, options);
  MICRONOTES_REQUIRE(counter(microcore::perf::CounterId::LayoutUnchangedUpdates) == before + 1);
}

// The reuse machinery is an optimisation, and the only thing that makes an
// optimisation safe is that it cannot be observed. This walks a document
// through the edits a person makes -- typing at each end and in the middle,
// deleting, splitting a block, resizing -- and after every one of them asserts
// that the incrementally updated layout is indistinguishable from one built
// from scratch for the same inputs.
//
// It is written as one long sequence rather than a test per edit on purpose:
// the incremental path carries state from the previous update, so the bugs it
// can have are the ones that need two edits in a row to show up.
MICRONOTES_TEST(layout_incremental_updates_match_a_layout_built_from_scratch) {
  std::string source = longFixture();
  DocumentLayout incremental;
  incremental.setMetrics(stubMetrics());

  std::uint64_t revision = 1;
  LayoutOptions options;
  options.width = 620.0f;

  int checked = 0;
  const auto settle = [&](const char* what) {
    options.sourceRevision = revision;
    incremental.update(source, options);

    DocumentLayout fresh;
    fresh.setMetrics(stubMetrics());
    LayoutOptions freshOptions = options;
    freshOptions.sourceRevision = 0;
    freshOptions.editedSpan = {};
    fresh.update(source, freshOptions);

    std::string why;
    // Compared before the message is built: the order the arguments of a call
    // are evaluated in is unspecified, so a message that reads `why` in the same
    // expression that fills it reads it empty about half the time.
    const bool agree = layoutsAgree(incremental, fresh, &why);
    micronotes::tests::require(agree,
                               std::string("incremental layout diverged -- ") + what + ": " + why);
    ++checked;
  };

  settle("first build");

  // Typing at the top, in the middle and at the end. Each leaves a different
  // amount of the document either side of the edit for the map to carry over.
  const std::size_t top = source.find("A paragraph") + 2;
  for(int i = 0; i < 4; ++i) {
    source.insert(top, 1, 'x');
    ++revision;
    settle("typing near the top");
  }
  std::size_t middle = source.find("A paragraph", source.size() / 2) + 2;
  for(int i = 0; i < 4; ++i) {
    source.insert(middle, 1, 'y');
    ++revision;
    settle("typing in the middle");
  }
  for(int i = 0; i < 4; ++i) {
    source.push_back('z');
    ++revision;
    settle("typing at the end");
  }

  // Deleting, which takes the other branch of every prefix/suffix comparison.
  for(int i = 0; i < 4; ++i) {
    source.erase(middle, 1);
    ++revision;
    settle("backspace in the middle");
  }

  // Splitting a block and joining it again: the block count changes, so every
  // block below shifts by an index as well as by an offset.
  source.insert(middle, "\n\n");
  ++revision;
  settle("splitting a paragraph");
  source.erase(middle, 2);
  ++revision;
  settle("joining it again");

  // Editing inside a fenced block, whose opening marker rides in front of the
  // first line of code rather than claiming a line of its own.
  const std::size_t fence = source.find("```cpp");
  MICRONOTES_REQUIRE(fence != std::string::npos);
  source.insert(fence + 6, 1, 'q');
  ++revision;
  settle("typing on a fence's info line");

  // Breaking a table's delimiter row and putting it back. A table is the one
  // construct here whose kind is decided by a line other than its first, and it
  // swings a whole block between `Complex` and `Paragraph` without changing its
  // extent -- the shape of edit most likely to catch a reuse map out.
  const std::size_t delimiter = source.find("|:------|------:|");
  MICRONOTES_REQUIRE(delimiter != std::string::npos);
  source.replace(delimiter, 1, "x");
  ++revision;
  settle("table delimiter broken");
  source.replace(delimiter, 1, "|");
  ++revision;
  settle("table delimiter restored");

  // Resizing, which invalidates every cached block at once, and then typing
  // again at the new width so the next edit reuses keys built under it.
  for(const float width : {480.0f, 900.0f, 620.0f}) {
    options.width = width;
    settle("resized");
    source.insert(middle, 1, 'w');
    ++revision;
    settle("typing after a resize");
  }

  MICRONOTES_REQUIRE(checked > 20);
}

MICRONOTES_TEST(layout_incremental_updates_match_under_a_random_edit_sequence) {
  for(const std::uint64_t seed : {0x9E3779B97F4A7C15ull, 0x1234567891234567ull,
                                  0xDEADBEEFCAFEF00Dull}) {
    walkRandomEdits(seed, 250);
  }
}

// A line ending inside a paragraph is one space on screen, however many bytes
// of newline and indentation the file spent on it. Asserted for a paragraph
// with no inline markup and for one with some, because those take two different
// tokenizers: a block the inline scanner finds nothing in skips the per-byte
// attribute table entirely, and the two paths have to agree about this or a
// hand-wrapped sentence gains its indentation back as visible spaces.
// An edit used to be located by comparing: forward to the first differing byte
// and backward to the first differing byte from the end, two passes summing to
// about the length of the note in order to find one typed character.
// `layout.edit_bytes_matched` read fourteen bytes for every byte that moved.
// The caller knows where it edited, so it says, and the comparison starts from
// the claim instead of from the ends.
MICRONOTES_TEST(layout_uses_the_callers_edited_span_instead_of_comparing_the_note) {
  std::string source = manyBlocks(2000);
  MICRONOTES_REQUIRE(source.size() > 100000);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  options.sourceRevision = 7;
  layout.update(source, options);

  using microcore::perf::CounterId;
  const auto usedBefore = counter(CounterId::LayoutEditSpansUsed);
  const auto bytesBefore = counter(CounterId::LayoutEditBytesCompared);
  const std::size_t at = source.size() / 2;
  source.insert(at, "z");
  options.sourceRevision = 8;
  options.editedSpan = {7, 8, at, at, at + 1};
  layout.update(source, options);
  const std::uint64_t used = counter(CounterId::LayoutEditSpansUsed) - usedBefore;
  const std::uint64_t bytes = counter(CounterId::LayoutEditBytesCompared) - bytesBefore;
  MICRONOTES_REQUIRE(used == 1);
  // The claim leaves only the bytes it did not exclude to be read. Comparing
  // would have read the whole note either side of the edit.
  micronotes::tests::require(bytes < 1024, "read " + std::to_string(bytes) +
                                             " bytes with a span that named the edit");

  // And the answer is still the answer.
  DocumentLayout fresh;
  fresh.setMetrics(stubMetrics());
  LayoutOptions freshOptions = options;
  freshOptions.sourceRevision = 0;
  freshOptions.editedSpan = {};
  fresh.update(source, freshOptions);
  std::string why;
  micronotes::tests::require(layoutsAgree(layout, fresh, &why), "claimed edit diverged: " + why);
}

// A claim is checked, not trusted. One stamped for a different pair of buffers
// -- two edits landed between two updates, which is what a frame handling two
// keystrokes looks like -- and one whose arithmetic cannot describe any pair of
// buffers are both discarded, and the full comparison runs.
MICRONOTES_TEST(layout_discards_an_edited_span_that_describes_another_pair_of_buffers) {
  using microcore::perf::CounterId;
  const auto run = [](const microcore::editor::TextEdit& claim, const std::string& expectedNote) {
    std::string source = manyBlocks(200);
    DocumentLayout layout;
    layout.setMetrics(stubMetrics());
    LayoutOptions options;
    options.width = 700.0f;
    options.sourceRevision = 3;
    layout.update(source, options);

    const auto comparedBefore = counter(CounterId::LayoutEditSpansCompared);
    // Two edits, one update: the buffer the layout holds is two revisions old.
    const std::size_t at = source.size() / 3;
    source.insert(at, "alpha ");
    source.insert(source.size() / 2, "beta ");
    options.sourceRevision = 5;
    options.editedSpan = claim;
    layout.update(source, options);
    micronotes::tests::require(
      counter(CounterId::LayoutEditSpansCompared) - comparedBefore == 1,
      expectedNote + ": the span should have been discarded");

    DocumentLayout fresh;
    fresh.setMetrics(stubMetrics());
    LayoutOptions freshOptions = options;
    freshOptions.sourceRevision = 0;
    freshOptions.editedSpan = {};
    fresh.update(source, freshOptions);
    std::string why;
    micronotes::tests::require(layoutsAgree(layout, fresh, &why), expectedNote + " diverged: " + why);
  };

  // Stamped for the second of the two edits only, so its `from` is not the
  // buffer the layout is standing on.
  run({4, 5, 100, 100, 106}, "a stamp from the wrong pair");
  // Right stamps, impossible arithmetic: the bytes it leaves outside itself do
  // not come to the same count on each side.
  run({3, 5, 100, 100, 101}, "a span whose sizes do not add up");
  // No stamps at all is what a caller with nothing to say offers.
  run({}, "an absent span");
}

// The visual-row index used to be a record per row, and both of its readers --
// "which row did this click land on" and "which row is the caret on" -- walked
// it from the front. So every click and every up-arrow in a long note cost a
// pass over every row in it. Nothing about the answers changed when that became
// a binary search over a per-block prefix sum, which is exactly why the probe
// counter exists: a linear scan and a binary search agree on every result, and
// on a fixture small enough to assert by hand they also agree on the time.
// The two claims of the incremental path, as counters rather than as timings:
// an edit rescans the *edit*, and it places the blocks the edit moved. Both are
// the difference between O(edit) and O(document), and both are invisible to a
// correctness test, because a full rescan and a full replacement produce exactly
// the right answer -- just slowly. So they are asserted here, on a document big
// enough that the two numbers cannot be confused.
MICRONOTES_TEST(layout_an_edit_rescans_and_replaces_only_what_it_touched) {
  std::string source = manyBlocks(400);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  options.sourceRevision = 1;
  layout.update(source, options);
  const std::size_t blocks = layout.blockCount();
  MICRONOTES_REQUIRE(blocks > 300);

  using microcore::perf::CounterId;
  // A character typed into a block in the middle of the document.
  const std::size_t at = source.find("Paragraph", source.size() / 2) + 3;
  MICRONOTES_REQUIRE(at != std::string::npos);
  source.insert(at, 1, 'x');
  options.sourceRevision = 2;

  const auto before = microcore::perf::captureCounters();
  layout.update(source, options);
  const auto after = microcore::perf::captureCounters();
  const auto delta = [&](CounterId id) {
    return after[static_cast<std::size_t>(id)] - before[static_cast<std::size_t>(id)];
  };

  // The scan resumes two blocks above the edit and stops at the first boundary
  // the old scan shared inside the untouched tail, so a handful either way --
  // but nowhere near the document.
  micronotes::tests::require(delta(CounterId::LayoutBlocksRescanned) > 0,
                             "the edit rescanned nothing at all");
  micronotes::tests::require(delta(CounterId::LayoutBlocksRescanned) < 12,
                             "rescanned " + std::to_string(delta(CounterId::LayoutBlocksRescanned)) +
                               " blocks of " + std::to_string(blocks));
  micronotes::tests::require(delta(CounterId::LayoutBlocksWalked) < 12,
                             "placed " + std::to_string(delta(CounterId::LayoutBlocksWalked)) +
                               " blocks of " + std::to_string(blocks));
  // And the source is patched over the edit rather than re-copied.
  micronotes::tests::require(delta(CounterId::LayoutSourceBytesCopied) < 64,
                             "copied " + std::to_string(delta(CounterId::LayoutSourceBytesCopied)) +
                               " bytes of " + std::to_string(source.size()));
  // The placement was patched, not rebuilt.
  MICRONOTES_REQUIRE(delta(CounterId::LayoutPlacementPatches) == 1);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutPlacementRebuilds) == 0);

  // Moving the caret between two blocks touches those two and nothing else --
  // the source does not move at all, so nothing is rescanned or copied.
  const auto beforeCaret = microcore::perf::captureCounters();
  layout.update(source, options);
  const auto afterCaret = microcore::perf::captureCounters();
  const auto caretDelta = [&](CounterId id) {
    return afterCaret[static_cast<std::size_t>(id)] - beforeCaret[static_cast<std::size_t>(id)];
  };
  MICRONOTES_REQUIRE(caretDelta(CounterId::LayoutBlocksScanned) == 0);
  MICRONOTES_REQUIRE(caretDelta(CounterId::LayoutSourceBytesCopied) == 0);
  micronotes::tests::require(caretDelta(CounterId::LayoutBlocksWalked) <= 4,
                             "a caret move placed " +
                               std::to_string(caretDelta(CounterId::LayoutBlocksWalked)) +
                               " blocks of " + std::to_string(blocks));
}

// The cache sweep erases most of the map while `placed_` still holds pointers
// into it. That is sound only because `unordered_map` is node-based -- erasing
// one element leaves pointers to every other element valid -- and the sweep
// leans on it hard enough to be worth a test: if it were ever wrong the symptom
// would be a use-after-free on the next frame rather than a failed assertion,
// which is why this reads the entire layout back afterwards and why it belongs
// in the sanitizer lanes.
MICRONOTES_TEST(layout_survives_a_cache_sweep_that_erases_most_of_the_map) {
  const std::string source = manyBlocks(120);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;

  using microcore::perf::CounterId;
  const auto before = counter(CounterId::LayoutCacheEvictions);
  // Every width is a new geometry, so every block gets a new key. A generation
  // is smaller than the block count here because identical blocks share a key,
  // which is the point of keying on content -- so it takes a dozen passes to
  // overflow a cache sized at three generations of *blocks*.
  for(int step = 0; step < 14; ++step) {
    options.width = 500.0f + static_cast<float>(step) * 37.0f;
    layout.update(source, options);
  }
  MICRONOTES_REQUIRE(counter(CounterId::LayoutCacheEvictions) > before);

  // Every block's layout has to still be readable and still agree with the
  // placement. This is the read that would fault on a freed node.
  float top = 0.0f;
  std::size_t rows = 0;
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    const auto& block = layout.layout(i);
    MICRONOTES_REQUIRE(std::abs(layout.blockTop(i) - top) < 0.01f);
    top += block.height;
    rows += block.lines.size();
    for(const auto& line : block.lines) {
      for(const auto& run : block.runsOf(line)) MICRONOTES_REQUIRE(run.srcEnd >= run.srcStart);
    }
  }
  MICRONOTES_REQUIRE(rows > 0);
  MICRONOTES_REQUIRE(std::abs(top - layout.totalHeight()) < 0.01f);
  MICRONOTES_REQUIRE(layout.offsetAt(80.0f, top * 0.5f) < source.size());
}

// Installing metrics throws the block cache away, and every entry in the
// standing placement is a pointer into that cache. Its one caller re-lays out
// immediately, so nothing reads the placement in between -- which is exactly
// the sort of invariant that holds until someone adds a second caller. This
// pins the safe behaviour: after new metrics and before the next update, the
// layout answers as an empty document instead of reading freed nodes.
MICRONOTES_TEST(layout_answers_as_empty_between_new_metrics_and_the_next_update) {
  const std::string source = manyBlocks(40);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.totalHeight() > 0.0f);

  layout.setMetrics(stubMetrics());
  MICRONOTES_REQUIRE(layout.totalHeight() == 0.0f);
  MICRONOTES_REQUIRE(layout.layout(0).lines.empty());
  MICRONOTES_REQUIRE(layout.offsetAt(100.0f, 400.0f) == 0);
  MICRONOTES_REQUIRE(layout.blockAt(400.0f) == std::nullopt);
  const auto range = layout.blockRange(0.0f, 1000.0f);
  MICRONOTES_REQUIRE(range.first == 0 && range.second == 0);
  MICRONOTES_REQUIRE(layout.caretRect(20).h > 0.0f);
  MICRONOTES_REQUIRE(layout.selectionRects(0, 40).empty());

  // And it comes back whole on the next update.
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.totalHeight() > 0.0f);
}

// The picture's box is part of the geometry, so a note that has one has to be
// laid out again when the window that decides its height changes -- and a
// texture that has finished loading has to be able to say so.
MICRONOTES_TEST(layout_relays_a_block_when_its_image_could_answer_differently) {
  const std::string source = "![a picture](pic.png)\n";
  float height = 100.0f;
  DocumentLayout layout;
  auto metrics = stubMetrics();
  metrics.measureImage = [&height](std::string_view, float column, float) {
    micronotes::doc::ImageBox box;
    box.width = std::min(column, 200.0f);
    box.height = height;
    return box;
  };
  layout.setMetrics(std::move(metrics));
  LayoutOptions options;
  options.width = 600.0f;
  options.imageMaxHeight = 400.0f;
  options.sourceRevision = 1;
  options.imageRevision = 1;
  layout.update(source, options);
  const float before = layout.layout(0).height;

  // The same stamp and the same bytes: the standing layout stands, whatever the
  // hook would answer now.
  height = 300.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.layout(0).height == before);

  options.imageRevision = 2;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.layout(0).height == before + 200.0f);
}

// Whether a `[[target]]` resolves decides a run's colour, and it is the one
// input to a block's layout that is not a function of the block's own bytes.
// The cache key is built from those bytes, so without a stamp for it the layout
// answers a changed library with last library's colour -- and goes on doing so
// until somebody happens to edit that block.
MICRONOTES_TEST(layout_recolours_a_wikilink_when_the_library_changes_under_it) {
  const std::string source = "A note that links to [[Somewhere]] in passing.\n";
  bool exists = false;
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 600.0f;
  options.wikiLinkResolves = [&exists](std::string_view) { return exists; };
  options.sourceRevision = 1;
  options.wikiLinkRevision = 1;
  layout.update(source, options);

  const auto roleOfTheLink = [&]() {
    for(std::size_t i = 0; i < layout.blockCount(); ++i) {
      const auto& block = layout.layout(i);
      for(const auto& line : block.lines) {
        for(const auto& run : block.runsOf(line)) {
          if(run.role == micronotes::doc::TextRole::WikiLink ||
             run.role == micronotes::doc::TextRole::WikiLinkUnresolved) {
            return run.role;
          }
        }
      }
    }
    return micronotes::doc::TextRole::Body;
  };
  MICRONOTES_REQUIRE(roleOfTheLink() == micronotes::doc::TextRole::WikiLinkUnresolved);

  // The note now exists. Not one byte of the buffer changed, and the caller
  // says so through the same source stamp -- the wikilink stamp is the only
  // thing that moved.
  exists = true;
  options.wikiLinkRevision = 2;
  layout.update(source, options);
  MICRONOTES_REQUIRE(roleOfTheLink() == micronotes::doc::TextRole::WikiLink);

  // And back, so this is not a one-way latch.
  exists = false;
  options.wikiLinkRevision = 3;
  layout.update(source, options);
  MICRONOTES_REQUIRE(roleOfTheLink() == micronotes::doc::TextRole::WikiLinkUnresolved);
}
