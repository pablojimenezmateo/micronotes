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

// What one laid-out block looks like: the runs it covers its source with, the
// markers it hides, the callout it titles, and where its lines break.

MICRONOTES_TEST(layout_covers_every_source_byte_with_a_run) {
  const std::string source = kFixture;
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 320.0f;
  options.revealAll = true;
  layout.update(source, options);

  std::size_t expected = 0;
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    const std::size_t base = layout.blocks()[i].start;
    const auto& blockLayout = layout.layout(i);
    for(const auto& line : blockLayout.lines) {
      for(const auto& run : blockLayout.runsOf(line)) {
        micronotes::tests::require(base + run.srcStart == expected, "run gap at " + std::to_string(base + run.srcStart) +
                                                                        ", expected " + std::to_string(expected));
        expected = base + run.srcEnd;
      }
    }
  }
  MICRONOTES_REQUIRE(expected == source.size());
}

MICRONOTES_TEST(layout_hides_markers_outside_the_caret_block) {
  const std::string source = "# Title\n\nBody **bold** text\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 400.0f;
  options.caretOffset = DocumentLayout::kNone;
  layout.update(source, options);

  const auto markerWidth = [&](std::size_t block) {
    float total = 0.0f;
    const auto& blockLayout = layout.layout(block);
    for(const auto& line : blockLayout.lines) {
      for(const auto& run : blockLayout.runsOf(line)) {
        if(run.isMarker) total += run.rect.w;
      }
    }
    return total;
  };
  MICRONOTES_REQUIRE(markerWidth(0) == 0.0f);
  MICRONOTES_REQUIRE(markerWidth(2) == 0.0f);

  options.caretOffset = 2;  // inside the heading
  layout.update(source, options);
  MICRONOTES_REQUIRE(markerWidth(0) > 0.0f);
  MICRONOTES_REQUIRE(markerWidth(2) == 0.0f);
}

// A callout's head line is its title. The layout says so, and says it only for
// the line that actually opens the run, so a three-line callout does not get
// three titles and a plain `>` quote does not get one at all.
MICRONOTES_TEST(layout_marks_only_the_head_of_a_callout_as_its_title) {
  const std::string source = "> [!NOTE] Read this\n> and then this\n\n> a plain quote\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  layout.update(source, options);

  std::size_t titles = 0;
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    if(layout.layout(i).calloutTitle) ++titles;
  }
  MICRONOTES_REQUIRE(titles == 1);
  MICRONOTES_REQUIRE(layout.layout(0).calloutTitle);
  MICRONOTES_REQUIRE(layout.blocks()[0].kind == BlockKind::Callout);
  MICRONOTES_REQUIRE(!layout.layout(1).calloutTitle);
}

// The title is drawn strong and in the callout's own colour, so while the caret
// is inside the block -- where the `> [!NOTE]` is shown as the text it really
// is -- it must stop being a title and go back to being source.
MICRONOTES_TEST(layout_stops_titling_a_callout_whose_markers_are_revealed) {
  const std::string source = "> [!TIP] Try this\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.layout(0).calloutTitle);

  options.caretOffset = 4;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.layout(0).revealed);
  MICRONOTES_REQUIRE(!layout.layout(0).calloutTitle);
}

// A quote with no `[!KIND]` is a quote. Titling it would put a bold coloured
// first line on every block quote in the library.
MICRONOTES_TEST(layout_never_titles_a_quote_that_names_no_kind) {
  const std::string source = "> just a quotation\n> over two lines\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  layout.update(source, options);
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    MICRONOTES_REQUIRE(!layout.layout(i).calloutTitle);
  }
}

// `![alt](target)` is an inline as far as the scanner is concerned, and the
// picture goes under the paragraph that named it. The layout has a buffer, not
// a texture cache, so it asks for the box -- exactly as it asks whether a
// wikilink resolves -- and reserves the height it is given, which is what lets
// every position query below that block already be right.
MICRONOTES_TEST(layout_reserves_room_under_a_block_for_its_images) {
  const std::string source = "Some prose.\n\n![a picture](pic.png)\n\nMore prose.\n";
  const auto build = [&source](bool withImages) {
    DocumentLayout layout;
    auto metrics = stubMetrics();
    if(withImages) {
      metrics.measureImage = [](std::string_view target, float column, float maxHeight) {
        micronotes::doc::ImageBox box;
        if(target != "pic.png") return box;
        box.width = std::min(column, 200.0f);
        box.height = std::min(maxHeight, 120.0f);
        return box;
      };
    }
    layout.setMetrics(std::move(metrics));
    LayoutOptions options;
    options.width = 600.0f;
    options.imageMaxHeight = 400.0f;
    layout.update(source, options);
    return layout;
  };
  const auto without = build(false);
  const auto with = build(true);
  // The block holding the image is the only one that grew, and it grew by the
  // picture plus the air either side of it.
  MICRONOTES_REQUIRE(with.blockCount() == without.blockCount());
  std::size_t imageBlock = DocumentLayout::kNone;
  for(std::size_t i = 0; i < with.blockCount(); ++i) {
    if(with.layout(i).images.empty()) {
      micronotes::tests::require(std::abs(with.layout(i).height - without.layout(i).height) < 0.001f,
                                 "a block with no image changed height");
      continue;
    }
    imageBlock = i;
    MICRONOTES_REQUIRE(with.layout(i).images.size() == 1);
    const auto& image = with.layout(i).images.front();
    MICRONOTES_REQUIRE(image.target == "pic.png");
    MICRONOTES_REQUIRE(image.rect.h == 120.0f);
    MICRONOTES_REQUIRE(image.rect.w == 200.0f);
    MICRONOTES_REQUIRE(with.layout(i).height > without.layout(i).height + 120.0f);
  }
  MICRONOTES_REQUIRE(imageBlock != DocumentLayout::kNone);
  // And everything under it moved down by exactly that much, which is the half
  // a picture drawn over the following blocks would have got wrong.
  const float grew = with.layout(imageBlock).height - without.layout(imageBlock).height;
  for(std::size_t i = imageBlock + 1; i < with.blockCount(); ++i) {
    micronotes::tests::require(std::abs(with.blockTop(i) - without.blockTop(i) - grew) < 0.001f,
                               "a block under the image did not move with it");
  }
}

// A line the writer ended is a line the reader sees ended.
//
// CommonMark folds a single newline inside a paragraph into a space, and this
// used to as well. A note is not a document being typeset: it is written in
// lines, and somebody who presses Enter once and keeps typing means the next
// word to start underneath rather than beside. The *text* is unchanged either
// way -- the newline is still one space's worth of run, so the source stays
// byte-aligned with what is drawn and a caret can be put on it -- and what
// changes is where the line ends.
MICRONOTES_TEST(layout_ends_a_line_where_the_writer_ended_one) {
  struct Drawn {
    std::string text;
    std::size_t lines = 0;
  };
  const auto drawn = [](const std::string& source) {
    DocumentLayout layout;
    layout.setMetrics(stubMetrics());
    LayoutOptions options;
    options.width = 4000.0f;  // wide enough that nothing wraps on screen
    layout.update(source, options);
    Drawn out;
    const auto& first = layout.layout(0);
    out.lines = first.lines.size();
    for(const auto& line : first.lines) {
      for(const auto& run : first.runsOf(line)) out.text += run.text;
    }
    return out;
  };

  // Every byte of the source is still drawn, in order, with the line ending as
  // the one space the file means.
  MICRONOTES_REQUIRE(drawn("A plain paragraph hand wrapped\n   across two source lines.\n").text ==
                     "A plain paragraph hand wrapped across two source lines.");
  MICRONOTES_REQUIRE(drawn("A **marked up** paragraph wrapped\n   across two source lines.\n").text ==
                     "A marked up paragraph wrapped across two source lines.");

  // Two source lines, two lines on screen, at a width where neither would have
  // wrapped on its own. Written without the file's own trailing newline: a
  // source that ends in one gets the document's empty caret line, which is a
  // separate rule and is pinned on its own below.
  MICRONOTES_REQUIRE(drawn("A plain paragraph hand wrapped\n   across two source lines.").lines == 2);
  MICRONOTES_REQUIRE(drawn("A **marked up** paragraph wrapped\n   across two source lines.").lines == 2);
  MICRONOTES_REQUIRE(drawn("one\ntwo\nthree").lines == 3);
  MICRONOTES_REQUIRE(drawn("just the one line").lines == 1);

  // The break is a *break*, not an empty line: two source lines are two lines,
  // and the file's terminating newline still buys exactly the one caret line it
  // always did rather than a second one.
  MICRONOTES_REQUIRE(drawn("just the one line\n").lines == 2);
  MICRONOTES_REQUIRE(drawn("A plain paragraph hand wrapped\n   across two source lines.\n").lines == 3);
}

// A collapsed block gives up every one of its visual rows, so the prefix sum
// behind the row index is full of runs of repeated values. Stepping the caret
// down by one row has to cross a whole run of them in a single move -- which is
// the case a "find the block at this row" written as a scan gets right by
// accident and one written as a search only gets right deliberately.
// The two searches in `caretRect` rest on one invariant: a block's runs are
// non-decreasing in both `srcStart` and `srcEnd`, and its lines own contiguous
// run ranges in order. If a future flow emits a run out of order the partition
// point silently returns the wrong run, so the invariant is asserted rather
// than assumed -- over the fixture, at a narrow measure, with markers revealed
// and hidden, which is every shape the flow has.
MICRONOTES_TEST(layout_runs_are_ordered_within_a_block) {
  for(const bool reveal : {false, true}) {
    for(const float width : {160.0f, 700.0f}) {
      const std::string source = std::string(kFixture) + oneHugeFence(40);
      DocumentLayout layout;
      layout.setMetrics(stubMetrics());
      LayoutOptions options;
      options.width = width;
      options.revealAll = reveal;
      layout.update(source, options);
      for(std::size_t i = 0; i < layout.blockCount(); ++i) {
        const auto& block = layout.layout(i);
        std::size_t lastStart = 0;
        std::size_t lastEnd = 0;
        for(const auto& run : block.runs) {
          micronotes::tests::require(run.srcStart >= lastStart, "run srcStart went backwards");
          micronotes::tests::require(run.srcEnd >= lastEnd, "run srcEnd went backwards");
          micronotes::tests::require(run.srcEnd >= run.srcStart, "run ends before it starts");
          lastStart = run.srcStart;
          lastEnd = run.srcEnd;
        }
        std::uint32_t nextRun = 0;
        for(const auto& line : block.lines) {
          micronotes::tests::require(line.runBegin >= nextRun, "line runs are not in order");
          micronotes::tests::require(line.runEnd >= line.runBegin, "line run range is inverted");
          micronotes::tests::require(line.runEnd <= block.runs.size(), "line runs escape the block");
          nextRun = line.runEnd;
        }
      }
    }
  }
}

// The tokenizer splits at every change of inline attribute as well as at every
// space, so `*soft*,` is two tokens with nothing between them. The
// wrap used to treat every token as a break opportunity, which put a lone comma
// at the head of a line whenever the measure fell there. A line may now only
// begin where the source has whitespace -- checked over a paragraph built so
// that every kind of inline markup abuts punctuation, and at several measures
// so the break falls in a different place in each.
MICRONOTES_TEST(layout_never_breaks_a_line_inside_a_word) {
  const std::string source =
    "Prose where **bold**, *soft*, `code`. and [link](a.md); and ~~gone~~! all\n"
    "abut punctuation, said again so the wrap has somewhere to fall: **bold**,\n"
    "*soft*, `code`. and [link](a.md); and ~~gone~~! once more for luck.\n";
  for(const bool reveal : {false, true}) {
    for(const float width : {200.0f, 260.0f, 317.0f, 480.0f}) {
      DocumentLayout layout;
      layout.setMetrics(stubMetrics());
      LayoutOptions options;
      options.width = width;
      options.revealAll = reveal;
      layout.update(source, options);
      for(std::size_t i = 0; i < layout.blockCount(); ++i) {
        const std::size_t base = layout.blocks()[i].start;
        const auto& block = layout.layout(i);
        for(std::size_t l = 1; l < block.lines.size(); ++l) {
          const auto runs = block.runsOf(block.lines[l]);
          if(runs.empty()) continue;
          const std::size_t at = base + runs.front().srcStart;
          micronotes::tests::require(at == 0 || isSpace(source[at - 1]),
                                     "line " + std::to_string(l) + " of block " +
                                         std::to_string(i) + " begins mid-word at offset " +
                                         std::to_string(at) + " (width " +
                                         std::to_string(width) + ")");
        }
      }
    }
  }
}

// The other half of the same decision: a cluster that cannot fit on a line of
// its own still has to break, rather than run off the right edge.
MICRONOTES_TEST(layout_breaks_a_cluster_too_wide_for_any_line) {
  const std::string source = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa**bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb**\n";
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 120.0f;
  options.revealAll = true;
  layout.update(source, options);
  const auto& block = layout.layout(0);
  MICRONOTES_REQUIRE(block.lines.size() > 1);
  for(const auto& line : block.lines) {
    float right = block.textLeft;
    for(const auto& run : block.runsOf(line)) right = run.rect.x + run.rect.w;
    micronotes::tests::require(right <= block.textLeft + options.width + 0.5f,
                               "a line overflowed the column: " + std::to_string(right));
  }
}

// Nothing scrolls sideways, so nothing may be wider than its column.
//
// A fenced code block and a block dropped to raw were the two that did not
// wrap: a long line ran off the right of its own column and stopped there. The
// clip kept it inside the column, which is the right treatment only if there is
// a way to follow it, and there was not.
//
// What separates a wrap from a line the file itself ended is
// `VisualLine::continuation`, and it has to: the mark that says "the column
// broke this" would otherwise appear on every hand-wrapped line in the note.
MICRONOTES_TEST(layout_wraps_a_code_line_too_long_for_its_column) {
  const auto lines = [](const std::string& source, float width) {
    DocumentLayout layout;
    layout.setMetrics(stubMetrics());
    LayoutOptions options;
    options.width = width;
    layout.update(source, options);
    return layout.layout(0).lines;
  };

  // Eight characters per glyph in the stub, so this line is far wider than the
  // column and has no space in it to break at.
  const std::string code = "```\nabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n```\n";
  const auto wrapped = lines(code, 200.0f);
  MICRONOTES_REQUIRE(wrapped.size() > 1);
  // The first is where the line started; every one after it continues that line.
  MICRONOTES_REQUIRE(!wrapped.front().continuation);
  bool anyContinuation = false;
  for(const auto& line : wrapped) anyContinuation = anyContinuation || line.continuation;
  MICRONOTES_REQUIRE(anyContinuation);

  // Wide enough for the whole line: one line, and nothing continues anything.
  const auto whole = lines(code, 4000.0f);
  for(const auto& line : whole) MICRONOTES_REQUIRE(!line.continuation);
}

// The other half of the same distinction: a line the *writer* ended is not a
// continuation, however many of them there are.
MICRONOTES_TEST(layout_does_not_call_a_writers_line_break_a_wrap) {
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 4000.0f;  // wide enough that nothing wraps on screen
  layout.update("one\ntwo\nthree", options);
  const auto& lines = layout.layout(0).lines;
  MICRONOTES_REQUIRE(lines.size() == 3);
  for(const auto& line : lines) MICRONOTES_REQUIRE(!line.continuation);
}
