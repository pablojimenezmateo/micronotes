#include "TestSupport.h"

#include "core/perf/PerformanceCounters.h"
#include "doc/Layout.h"

#include <cmath>
#include <cstdint>
#include <string>

using micronotes::doc::BlockKind;
using micronotes::doc::DocumentLayout;
using micronotes::doc::LayoutOptions;
using micronotes::doc::Metrics;
using micronotes::doc::RunStyle;

namespace {

// A stub font: every codepoint is the same width, so measurement is additive
// and offset round-tripping is exact.
float stubMeasure(std::string_view value, const RunStyle& style) {
  std::size_t glyphs = 0;
  for(const char c : value) {
    if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
  }
  return static_cast<float>(glyphs) * style.size * 0.6f;
}

Metrics stubMetrics() {
  Metrics metrics;
  metrics.measure = stubMeasure;
  metrics.lineHeight = [](const RunStyle& style) { return std::round(style.size * 1.5f); };
  return metrics;
}

const char* kFixture =
  "# Live surface\n"
  "\n"
  "A paragraph with **strong**, *soft*, `code`, ~~gone~~ and a [link](docs/a.md)\n"
  "that wraps across two source lines and is long enough to wrap on screen too.\n"
  "\n"
  "## Lists\n"
  "\n"
  "- bullet one\n"
  "- an item hand-wrapped across source lines, whose newline and the\n"
  "  indentation continuing it fold into one space\n"
  "- [ ] a task\n"
  "  - nested bullet with rather a lot of words in it to force a visual wrap\n"
  "1. ordered one\n"
  "\n"
  "> quoted text\n"
  "\n"
  "```cpp\n"
  "int main() { return 0; }\n"
  "\n"
  "```\n"
  "\n"
  "---\n"
  "\n"
  "Final caf\xc3\xa9 paragraph.\n";

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// A line ending inside a block - the newline plus the indentation of the line
// continuing it - is drawn as the one space the file means, so the offsets
// inside it share a position and clicking there can only reach the last of
// them. Everywhere else the round trip is the identity; this says exactly where
// it is not, rather than letting the assertion go soft.
bool insideOneFoldedLineEnding(const std::string& text, std::size_t a, std::size_t b) {
  if(a == b) return true;
  std::size_t lo = a < b ? a : b;
  std::size_t hi = a < b ? b : a;
  for(std::size_t i = lo; i < hi; ++i) {
    if(!isSpace(text[i])) return false;
  }
  while(lo > 0 && isSpace(text[lo - 1])) --lo;
  while(hi < text.size() && isSpace(text[hi])) ++hi;
  return text.find('\n', lo) < hi;
}

std::size_t nextBoundary(const std::string& text, std::size_t index) {
  std::size_t next = index + 1;
  while(next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
  return next;
}

}

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
    for(const auto& line : layout.layout(i).lines) {
      for(const auto& run : line.runs) {
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
    for(const auto& line : layout.layout(block).lines) {
      for(const auto& run : line.runs) {
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
  for(std::size_t offset = block.start; offset < block.end; ++offset) {
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

namespace {

// A document long enough that walking it and walking the viewport are visibly
// different numbers.
std::string manyBlocks(int sections) {
  std::string out;
  for(int i = 0; i < sections; ++i) {
    out += "## Section " + std::to_string(i) + "\n\n";
    out += "A paragraph with enough words in it to occupy a line of its own.\n\n";
    out += "- bullet " + std::to_string(i) + "\n\n";
  }
  return out;
}

std::uint64_t counter(microcore::perf::CounterId id) {
  return microcore::perf::readCounter(id);
}

}

// The live surface re-lays the note out once per frame whether or not anything
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
  // Not one byte copied, not one block rescanned or rehashed, not one line
  // reflattened. These are the four O(document) passes the fast path exists to
  // skip, and each of them reading zero is the whole claim.
  MICRONOTES_REQUIRE(delta(CounterId::LayoutSourceBytesCopied) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutKeyBytesHashed) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutBlocksScanned) == 0);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutFlatLinesBuilt) == 0);
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

  LayoutOptions caret = narrower;
  caret.caretOffset = source.size() / 2;
  MICRONOTES_REQUIRE(rebuilds(caret, source));

  LayoutOptions revealed = caret;
  revealed.revealAll = true;
  MICRONOTES_REQUIRE(rebuilds(revealed, source));

  const std::string edited = source + "\nA new paragraph.\n";
  MICRONOTES_REQUIRE(rebuilds(revealed, edited));

  // Same length, different bytes: a length check alone would miss this, and the
  // screen would keep the old text.
  std::string swapped = edited;
  swapped[swapped.size() / 2] = swapped[swapped.size() / 2] == 'x' ? 'y' : 'x';
  MICRONOTES_REQUIRE(rebuilds(revealed, swapped));

  // New faces mean every cached block was measured with the wrong ones, and
  // neither the source nor the geometry says so.
  layout.update(swapped, revealed);
  layout.setMetrics(stubMetrics());
  MICRONOTES_REQUIRE(rebuilds(revealed, swapped));
}

// A fold collapses without the source, the geometry or the caret moving, so it
// is the one input the reuse check has to resolve rather than compare.
MICRONOTES_TEST(layout_update_rebuilds_when_a_fold_closes) {
  const std::string source = manyBlocks(8);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  bool collapsed = false;
  options.folded = [&collapsed](const micronotes::doc::SourceBlock& block) {
    return collapsed && block.kind == BlockKind::Heading;
  };
  layout.update(source, options);
  const float open = layout.totalHeight();

  collapsed = true;
  const auto before = counter(microcore::perf::CounterId::LayoutUnchangedUpdates);
  layout.update(source, options);
  MICRONOTES_REQUIRE(counter(microcore::perf::CounterId::LayoutUnchangedUpdates) == before);
  MICRONOTES_REQUIRE(layout.totalHeight() < open);
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
