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
using micronotes::tests::insideOneFoldedLineEnding;
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

// Queries over a laid-out document: where an offset is on screen, where a point
// lands, which rows a selection covers, and how the caret steps between them.
// `doc/LayoutQueries.cpp` is the unit; these are its answers.

MICRONOTES_TEST(layout_round_trips_every_offset) {
  const std::string source = kFixture;
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 320.0f;
  options.revealAll = true;
  layout.update(source, options);

  for(std::size_t offset = 0; offset <= source.size(); offset = nextBoundary(source, offset)) {
    const auto caret = layout.caretRect(offset);
    const auto back = layout.offsetAt(caret.x, caret.y + caret.h / 2.0f);
    micronotes::tests::require(back == offset || insideOneFoldedLineEnding(source, offset, back),
                               "offset " + std::to_string(offset) + " round-tripped to " +
                                   std::to_string(back) + " via x=" + std::to_string(caret.x) +
                                   " y=" + std::to_string(caret.y));
  }
}

MICRONOTES_TEST(layout_round_trips_at_a_narrow_measure) {
  const std::string source = kFixture;
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 160.0f;
  options.revealAll = true;
  layout.update(source, options);
  for(std::size_t offset = 0; offset <= source.size(); offset = nextBoundary(source, offset)) {
    const auto caret = layout.caretRect(offset);
    const auto back = layout.offsetAt(caret.x, caret.y + caret.h / 2.0f);
    micronotes::tests::require(back == offset || insideOneFoldedLineEnding(source, offset, back),
                               "narrow: offset " + std::to_string(offset) + " -> " + std::to_string(back));
  }
}

MICRONOTES_TEST(layout_moves_the_caret_by_visual_rows) {
  const std::string source = "alpha bravo charlie delta echo foxtrot golf hotel india juliet\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 120.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.layout(0).lines.size() > 2);

  const std::size_t down = layout.rowRelative(2, 1);
  MICRONOTES_REQUIRE(down > 2);
  const auto first = layout.caretRect(2);
  const auto second = layout.caretRect(down);
  MICRONOTES_REQUIRE(second.y > first.y);
  MICRONOTES_REQUIRE(std::abs(second.x - first.x) < 8.0f);
  MICRONOTES_REQUIRE(layout.rowRelative(down, -1) == 2);
}

MICRONOTES_TEST(layout_reports_selection_rectangles_per_line) {
  const std::string source = "alpha bravo charlie delta echo foxtrot golf hotel\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 120.0f;
  layout.update(source, options);
  // The buffer ends in a newline, so the last visual line is the empty one the
  // caret can sit on and it contributes no selection rectangle.
  const auto rects = layout.selectionRects(0, source.size() - 1);
  MICRONOTES_REQUIRE(rects.size() + 1 == layout.layout(0).lines.size());
  for(const auto& rect : rects) MICRONOTES_REQUIRE(rect.w > 0.0f);
  MICRONOTES_REQUIRE(layout.selectionRects(4, 4).empty());
}

// The band is a filter over the rects, not a different answer. Anything the
// banded call returns has to be exactly what the unbanded one returned for the
// same rows -- otherwise a scroll would repaint the selection differently from
// how it was painted a frame ago.
MICRONOTES_TEST(layout_bands_a_selection_to_the_rows_that_are_on_screen) {
  std::string source;
  // Long enough that a window's worth is a small fraction of it, and mixed
  // enough that some blocks wrap and some do not.
  for(int i = 0; i < 200; ++i) {
    source += "## Heading\n\nalpha bravo charlie delta echo foxtrot golf hotel india juliet\n\n- item\n\n";
  }
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 200.0f;
  layout.update(source, options);

  const auto all = layout.selectionRects(0, source.size());
  MICRONOTES_REQUIRE(all.size() > 500);

  // Several bands across the document, including ones that start and end inside
  // a block rather than on a boundary.
  for(float top : {0.0f, 137.0f, layout.totalHeight() / 3.0f, layout.totalHeight() - 400.0f}) {
    const float bottom = top + 480.0f;
    std::vector<Rect> banded;
    layout.selectionRectsInto(0, source.size(), top, bottom, &banded);
    // Exactly the rects of `all` whose rows intersect the band, in order.
    std::vector<Rect> expected;
    for(const auto& rect : all) {
      if(rect.y + rect.h > top && rect.y < bottom) expected.push_back(rect);
    }
    MICRONOTES_REQUIRE(banded.size() == expected.size());
    MICRONOTES_REQUIRE(!banded.empty());
    for(std::size_t i = 0; i < banded.size(); ++i) {
      MICRONOTES_REQUIRE(banded[i].x == expected[i].x);
      MICRONOTES_REQUIRE(banded[i].y == expected[i].y);
      MICRONOTES_REQUIRE(banded[i].w == expected[i].w);
      MICRONOTES_REQUIRE(banded[i].h == expected[i].h);
    }
    // And the band really is a small part of the whole, or this proves nothing.
    MICRONOTES_REQUIRE(banded.size() * 10 < all.size());
  }
}

// The toolbar's two rects have to be the selection's own first and last, however
// much of the note it covers and wherever the viewport happens to be -- the
// toolbar is placed against the selection, not against what is on screen.
MICRONOTES_TEST(layout_finds_a_selections_ends_without_building_the_middle) {
  std::string source;
  for(int i = 0; i < 120; ++i) source += "alpha bravo charlie delta echo\n\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 200.0f;
  layout.update(source, options);

  const std::size_t spans[][2] = {
    {0, source.size()},
    {0, 6},
    {5, 400},
    {source.size() / 2, source.size() / 2 + 90},
  };
  for(const auto& span : spans) {
    const auto all = layout.selectionRects(span[0], span[1]);
    const auto ends = layout.selectionEnds(span[0], span[1]);
    MICRONOTES_REQUIRE(all.empty() == !ends.has_value());
    if(all.empty()) continue;
    MICRONOTES_REQUIRE(ends->first.y == all.front().y);
    MICRONOTES_REQUIRE(ends->first.x == all.front().x);
    MICRONOTES_REQUIRE(ends->second.y == all.back().y);
    MICRONOTES_REQUIRE(ends->second.x == all.back().x);
  }
  // An empty selection places nothing.
  MICRONOTES_REQUIRE(!layout.selectionEnds(10, 10).has_value());
}

MICRONOTES_TEST(layout_gives_complex_blocks_one_caret_position) {
  const std::string source = "text\n\n| a | b |\n|---|---|\n| 1 | 2 |\n";
  DocumentLayout layout;
  Metrics metrics = stubMetrics();
  metrics.measureComplex = [](const micronotes::doc::SourceBlock&, float) { return 90.0f; };
  layout.setMetrics(metrics);
  LayoutOptions options;
  options.width = 400.0f;
  layout.update(source, options);

  const std::size_t table = layout.blockCount() - 1;
  MICRONOTES_REQUIRE(layout.layout(table).complex);
  MICRONOTES_REQUIRE(layout.layout(table).height >= 90.0f);
  const auto& block = layout.blocks()[table];
  for(std::size_t offset = block.start; offset < block.end(); ++offset) {
    MICRONOTES_REQUIRE(layout.offsetAt(400.0f, layout.blockTop(table) + 1.0f) == block.start);
  }

  options.rawOffset = block.start;
  layout.update(source, options);
  MICRONOTES_REQUIRE(!layout.layout(table).complex);
  MICRONOTES_REQUIRE(layout.layout(table).lines.size() == 4);  // three source lines plus the trailing caret line
}

MICRONOTES_TEST(layout_finds_the_block_under_a_point) {
  const std::string source = "# One\n\nTwo\n\n- three\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  layout.update(source, options);
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    const float middle = layout.blockTop(i) + layout.layout(i).height / 2.0f;
    const auto found = layout.blockAt(middle);
    MICRONOTES_REQUIRE(found && *found == i);
  }
  MICRONOTES_REQUIRE(!layout.blockAt(-4.0f));
  MICRONOTES_REQUIRE(!layout.blockAt(layout.totalHeight() + 4.0f));
}

// What lets a draw pass cost the viewport instead of the document.
MICRONOTES_TEST(layout_block_range_covers_the_band_and_nothing_else) {
  const std::string source = manyBlocks(100);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.blockCount() > 200);

  const auto range = layout.blockRange(0.0f, 200.0f);
  MICRONOTES_REQUIRE(range.first == 0);
  MICRONOTES_REQUIRE(range.second < layout.blockCount());

  // Every block the band touches is inside the range...
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    const float top = layout.blockTop(i);
    const float bottom = top + layout.layout(i).height;
    if(bottom <= 0.0f || top >= 200.0f) continue;
    MICRONOTES_REQUIRE(i >= range.first && i < range.second);
  }
  // ...and a band in the middle starts somewhere in the middle.
  const float half = layout.totalHeight() / 2.0f;
  const auto middle = layout.blockRange(half, half + 100.0f);
  MICRONOTES_REQUIRE(middle.first > 0);
  MICRONOTES_REQUIRE(middle.second > middle.first);
  MICRONOTES_REQUIRE(layout.blockTop(middle.first) <= half);

  // A band past the end is empty rather than out of bounds.
  const auto past = layout.blockRange(layout.totalHeight() + 1000.0f,
                                      layout.totalHeight() + 2000.0f);
  MICRONOTES_REQUIRE(past.second <= layout.blockCount());
}

MICRONOTES_TEST(layout_row_lookups_binary_search_the_index) {
  const std::string source = manyBlocks(400);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;

  using microcore::perf::CounterId;
  const auto rowsBefore = counter(CounterId::LayoutVisualRows);
  layout.update(source, options);
  const std::uint64_t rows = counter(CounterId::LayoutVisualRows) - rowsBefore;
  MICRONOTES_REQUIRE(rows > 1000);

  const auto queriesBefore = counter(CounterId::LayoutRowIndexQueries);
  const auto probesBefore = counter(CounterId::LayoutRowIndexProbes);
  const std::size_t hit = layout.offsetAt(80.0f, layout.totalHeight() * 0.75f);
  const std::uint64_t queries = counter(CounterId::LayoutRowIndexQueries) - queriesBefore;
  const std::uint64_t probes = counter(CounterId::LayoutRowIndexProbes) - probesBefore;

  // The answer still has to be right: three quarters of the way down a uniform
  // document is three quarters of the way through its bytes.
  MICRONOTES_REQUIRE(hit > source.size() / 2);
  MICRONOTES_REQUIRE(hit < source.size());
  MICRONOTES_REQUIRE(queries == 1);
  // One query is ceil(log2(rows)) + 1 steps at most. The second bound is the
  // one that matters: a walk would take `rows` of them.
  MICRONOTES_REQUIRE(probes < 24);
  MICRONOTES_REQUIRE(probes * 40 < rows);
}

// The caret is drawn once per frame, and its block used to be walked row by row
// -- 7.1 us for a caret at the end of a 4,000-line fence, which is one block.
// Both the run and its owning line are found by partition point now, so the
// probe count is logarithmic in the block and the answer is byte-identical to
// the walk it replaced.
MICRONOTES_TEST(layout_caret_lookups_binary_search_the_runs) {
  const std::string source = oneHugeFence(2000);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.blockCount() < 4);
  MICRONOTES_REQUIRE(layout.layout(0).lines.size() > 1500);

  using microcore::perf::CounterId;
  const auto queriesBefore = counter(CounterId::LayoutCaretQueries);
  const auto probesBefore = counter(CounterId::LayoutCaretProbes);
  const Rect atEnd = layout.caretRect(source.size() - 4);
  const std::uint64_t queries = counter(CounterId::LayoutCaretQueries) - queriesBefore;
  const std::uint64_t probes = counter(CounterId::LayoutCaretProbes) - probesBefore;
  MICRONOTES_REQUIRE(queries == 1);
  // Two searches, each at most ceil(log2(n)) + 1 steps. The walk took one step
  // per run plus one per row, which here is thousands.
  MICRONOTES_REQUIRE(probes < 40);
  MICRONOTES_REQUIRE(atEnd.y > layout.totalHeight() * 0.9f);

  const Rect reference = caretRectByWalking(layout, source.size() - 4);
  micronotes::tests::require(std::abs(atEnd.x - reference.x) < 0.001f &&
                                 std::abs(atEnd.y - reference.y) < 0.001f &&
                                 std::abs(atEnd.h - reference.h) < 0.001f,
                             "caret at the end of a fence disagrees with the walk");
}

// ...and it agrees with the walk everywhere, not only at the end of a fence:
// every code-point boundary of the fixture, at both measures, revealed and not.
MICRONOTES_TEST(layout_caret_search_agrees_with_the_walk_everywhere) {
  for(const bool reveal : {false, true}) {
    for(const float width : {160.0f, 700.0f}) {
      const std::string source = std::string(kFixture) + oneHugeFence(12);
      DocumentLayout layout;
      layout.setMetrics(stubMetrics());
      LayoutOptions options;
      options.width = width;
      options.revealAll = reveal;
      layout.update(source, options);
      for(std::size_t offset = 0; offset <= source.size(); offset = nextBoundary(source, offset)) {
        const Rect got = layout.caretRect(offset);
        const Rect want = caretRectByWalking(layout, offset);
        micronotes::tests::require(std::abs(got.x - want.x) < 0.001f &&
                                       std::abs(got.y - want.y) < 0.001f &&
                                       std::abs(got.h - want.h) < 0.001f,
                                   "caret disagrees with the walk at offset " + std::to_string(offset));
      }
    }
  }
}

MICRONOTES_TEST(layout_row_motion_steps_over_collapsed_blocks) {
  const std::string source = manyBlocks(6);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  options.folded = [](const micronotes::doc::SourceBlock& block) {
    return block.kind == BlockKind::Heading;
  };
  layout.update(source, options);

  std::size_t rows = 0;
  for(std::size_t i = 0; i < layout.blockCount(); ++i) rows += layout.layout(i).lines.size();
  // Most blocks are inside a collapsed heading and contribute nothing.
  MICRONOTES_REQUIRE(rows > 1);
  MICRONOTES_REQUIRE(rows < layout.blockCount());

  std::size_t offset = 0;
  float y = layout.caretRect(0).y;
  for(std::size_t step = 0; step + 1 < rows; ++step) {
    const std::size_t next = layout.rowRelative(offset, 1);
    MICRONOTES_REQUIRE(next > offset);
    const auto rect = layout.caretRect(next);
    MICRONOTES_REQUIRE(rect.y > y);
    offset = next;
    y = rect.y;
  }
  // And a row past the last one saturates at the end of the buffer rather than
  // running off the index.
  MICRONOTES_REQUIRE(layout.rowRelative(offset, 1) == source.size());
  MICRONOTES_REQUIRE(layout.rowRelative(0, -1) == 0);
}
