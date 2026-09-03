#include "TestSupport.h"

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

// The stamps are the caller saying "nothing moved" so the layout does not have
// to prove it. Proving it costs a memcmp of the whole note and a fold query per
// block, which on a 466 KB note was the entire cost of an idle frame.
MICRONOTES_TEST(layout_stamped_reuse_asks_the_fold_predicate_nothing) {
  const std::string source = manyBlocks(200);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  options.sourceRevision = 7;
  options.foldRevision = 3;
  int asked = 0;
  options.folded = [&asked](const micronotes::doc::SourceBlock&) {
    ++asked;
    return false;
  };
  layout.update(source, options);
  MICRONOTES_REQUIRE(asked > 0);  // the first pass has to resolve them

  asked = 0;
  const auto before = microcore::perf::captureCounters();
  for(int frame = 0; frame < 10; ++frame) layout.update(source, options);
  const auto after = microcore::perf::captureCounters();
  using microcore::perf::CounterId;
  const auto delta = [&](CounterId id) {
    return after[static_cast<std::size_t>(id)] - before[static_cast<std::size_t>(id)];
  };
  MICRONOTES_REQUIRE(delta(CounterId::LayoutUnchangedUpdates) == 10);
  MICRONOTES_REQUIRE(delta(CounterId::LayoutFoldQueries) == 0);
  MICRONOTES_REQUIRE(asked == 0);
}

// A stamp that moves has to invalidate, or a collapsed heading stays open and
// the screen is simply wrong.
MICRONOTES_TEST(layout_a_moved_fold_stamp_re_resolves_the_folds) {
  const std::string source = manyBlocks(8);
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 700.0f;
  options.sourceRevision = 1;
  options.foldRevision = 1;
  bool collapsed = false;
  options.folded = [&collapsed](const micronotes::doc::SourceBlock& block) {
    return collapsed && block.kind == BlockKind::Heading;
  };
  layout.update(source, options);
  const float open = layout.totalHeight();

  collapsed = true;
  options.foldRevision = 2;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.totalHeight() < open);

  collapsed = false;
  options.foldRevision = 3;
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.totalHeight() == open);
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

namespace {

// Everything about a laid-out document that anything downstream can observe:
// where each block sits, how tall it is, and every run on every line of it.
// Two layouts that agree here are interchangeable to the view, the caret and
// the hit tester.
bool layoutsAgree(const DocumentLayout& a, const DocumentLayout& b, std::string* why) {
  const auto fail = [&](const std::string& message) {
    if(why) *why = message;
    return false;
  };
  if(a.blockCount() != b.blockCount()) return fail("block count");
  for(std::size_t i = 0; i < a.blockCount(); ++i) {
    const auto& left = a.layout(i);
    const auto& right = b.layout(i);
    const std::string at = " at block " + std::to_string(i);
    if(std::abs(a.blockTop(i) - b.blockTop(i)) > 0.001f) return fail("block top" + at);
    if(a.blockHidden(i) != b.blockHidden(i)) return fail("hidden" + at);
    if(left.kind != right.kind) return fail("kind" + at);
    if(std::abs(left.height - right.height) > 0.001f) return fail("height" + at);
    if(std::abs(left.indent - right.indent) > 0.001f) return fail("indent" + at);
    if(std::abs(left.textLeft - right.textLeft) > 0.001f) return fail("text left" + at);
    if(left.revealed != right.revealed) return fail("revealed" + at);
    if(left.raw != right.raw) return fail("raw" + at);
    if(left.complex != right.complex) return fail("complex" + at);
    if(left.calloutTitle != right.calloutTitle) return fail("callout title" + at);
    if(left.links != right.links) return fail("links" + at);
    if(left.lines.size() != right.lines.size()) return fail("line count" + at);
    for(std::size_t l = 0; l < left.lines.size(); ++l) {
      const auto& leftLine = left.lines[l];
      const auto& rightLine = right.lines[l];
      const std::string on = at + " line " + std::to_string(l);
      if(std::abs(leftLine.y - rightLine.y) > 0.001f) return fail("line y" + on);
      if(std::abs(leftLine.height - rightLine.height) > 0.001f) return fail("line height" + on);
      const auto leftRuns = left.runsOf(leftLine);
      const auto rightRuns = right.runsOf(rightLine);
      if(leftRuns.size() != rightRuns.size()) return fail("run count" + on);
      for(std::size_t r = 0; r < leftRuns.size(); ++r) {
        const auto& leftRun = leftRuns[r];
        const auto& rightRun = rightRuns[r];
        const std::string in = on + " run " + std::to_string(r);
        if(leftRun.srcStart != rightRun.srcStart) return fail("run start" + in);
        if(leftRun.srcEnd != rightRun.srcEnd) return fail("run end" + in);
        if(leftRun.text != rightRun.text) return fail("run text" + in);
        if(leftRun.role != rightRun.role) return fail("run role" + in);
        if(leftRun.isMarker != rightRun.isMarker) return fail("run marker" + in);
        if(leftRun.linkIndex != rightRun.linkIndex) return fail("run link" + in);
        if(!(leftRun.style == rightRun.style)) return fail("run style" + in);
        if(std::abs(leftRun.rect.x - rightRun.rect.x) > 0.001f) return fail("run x" + in);
        if(std::abs(leftRun.rect.w - rightRun.rect.w) > 0.001f) return fail("run w" + in);
      }
    }
  }

  // ...and every query the surface actually asks, because the placement is
  // patched in place now rather than rebuilt, and a patch can leave a *correct*
  // block sitting at a stale row. The per-block loop above cannot see that: the
  // row index is a separate array, and nothing in `layout(i)` reads it. These
  // are the readers of it.
  const std::string_view text = a.source();
  const float height = a.totalHeight();
  const std::size_t rowStep = text.size() / 64 + 1;
  for(std::size_t offset = 0; offset <= text.size(); offset += rowStep) {
    const std::string at = " at offset " + std::to_string(offset);
    if(a.rowRelative(offset, 1) != b.rowRelative(offset, 1)) return fail("row down" + at);
    if(a.rowRelative(offset, -1) != b.rowRelative(offset, -1)) return fail("row up" + at);
    if(a.rowRelative(offset, 9) != b.rowRelative(offset, 9)) return fail("row down nine" + at);
    const Rect left = a.caretRect(offset);
    const Rect right = b.caretRect(offset);
    if(std::abs(left.x - right.x) > 0.001f || std::abs(left.y - right.y) > 0.001f ||
       std::abs(left.h - right.h) > 0.001f) {
      return fail("caret rect" + at);
    }
    const auto leftRects = a.selectionRects(offset, offset + rowStep);
    const auto rightRects = b.selectionRects(offset, offset + rowStep);
    if(leftRects.size() != rightRects.size()) return fail("selection rect count" + at);
    for(std::size_t r = 0; r < leftRects.size(); ++r) {
      if(std::abs(leftRects[r].x - rightRects[r].x) > 0.001f ||
         std::abs(leftRects[r].y - rightRects[r].y) > 0.001f ||
         std::abs(leftRects[r].w - rightRects[r].w) > 0.001f ||
         std::abs(leftRects[r].h - rightRects[r].h) > 0.001f) {
        return fail("selection rect" + at);
      }
    }
  }
  const float yStep = height / 48.0f + 1.0f;
  for(float y = -20.0f; y < height + 40.0f; y += yStep) {
    const std::string at = " at y " + std::to_string(y);
    if(a.blockAt(y) != b.blockAt(y)) return fail("block at" + at);
    if(a.blockRange(y, y + 120.0f) != b.blockRange(y, y + 120.0f)) return fail("block range" + at);
    for(const float x : {-10.0f, 0.0f, 37.0f, 240.0f, 4000.0f}) {
      if(a.offsetAt(x, y) != b.offsetAt(x, y)) {
        return fail("offset at x " + std::to_string(x) + at);
      }
    }
  }
  if(a.rowsPerHeight(400.0f) != b.rowsPerHeight(400.0f)) return fail("rows per height");
  if(std::abs(a.totalHeight() - b.totalHeight()) > 0.001f) return fail("total height");
  return true;
}

// A document long enough that the incremental path has a prefix and a suffix to
// carry over, and varied enough that the blocks it carries are not all alike.
std::string sectionedFixture(int sections) {
  std::string source;
  for(int section = 0; section < sections; ++section) {
    const std::string n = std::to_string(section);
    source += "## Section " + n + "\n\n";
    source += "A paragraph with **strong text**, `code`, a [link](note-" + n +
              ".md) and enough words in it to wrap more than once on screen.\n\n";
    source += "- bullet " + n + "\n- [ ] task " + n + "\n\n";
    source += "> quoted line " + n + "\n\n";
    source += "```cpp\nint value_" + n + " = " + n + ";\n```\n\n";
    // A table, because whether its first row is a table at all is decided by
    // the line under it. That makes it the one construct here whose kind can
    // change while its own bytes do not, which is what the block-shape check in
    // the reuse map is for.
    source += "| left " + n + " | right |\n|:------|------:|\n| a | b |\n\n";
  }
  return source;
}

std::string longFixture() {
  return sectionedFixture(40);
}

}

// The reuse machinery is an optimisation, and the only thing that makes an
// optimisation safe is that it cannot be observed. This walks a document
// through the edits a person makes -- typing at each end and in the middle,
// deleting, splitting a block, moving the caret, folding, resizing -- and after
// every one of them asserts that the incrementally updated layout is
// indistinguishable from one built from scratch for the same inputs.
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
    freshOptions.foldRevision = 0;
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
    options.caretOffset = top + 1;
    settle("typing near the top");
  }
  std::size_t middle = source.find("A paragraph", source.size() / 2) + 2;
  for(int i = 0; i < 4; ++i) {
    source.insert(middle, 1, 'y');
    ++revision;
    options.caretOffset = middle + 1;
    settle("typing in the middle");
  }
  for(int i = 0; i < 4; ++i) {
    source.push_back('z');
    ++revision;
    options.caretOffset = source.size();
    settle("typing at the end");
  }

  // Deleting, which takes the other branch of every prefix/suffix comparison.
  for(int i = 0; i < 4; ++i) {
    source.erase(middle, 1);
    ++revision;
    options.caretOffset = middle;
    settle("backspace in the middle");
  }

  // Splitting a block and joining it again: the block count changes, so every
  // block below shifts by an index as well as by an offset.
  source.insert(middle, "\n\n");
  ++revision;
  options.caretOffset = middle + 2;
  settle("splitting a paragraph");
  source.erase(middle, 2);
  ++revision;
  options.caretOffset = middle;
  settle("joining it again");

  // Moving the caret with no edit at all: the source stands still and two
  // blocks change which of their markers are shown.
  for(std::size_t i = 0; i < incremental.blockCount(); i += 7) {
    options.caretOffset = incremental.blocks()[i].start;
    settle("moving the caret");
  }

  // Into and out of a fenced block, whose opening marker moves between being a
  // line of its own and riding in front of the first line of code.
  const std::size_t fence = source.find("```cpp");
  MICRONOTES_REQUIRE(fence != std::string::npos);
  options.caretOffset = fence + 2;
  settle("caret inside a fence");
  options.rawOffset = fence + 2;
  settle("fence dropped to raw");
  options.rawOffset = DocumentLayout::kNone;
  options.caretOffset = 0;
  settle("back out of the fence");

  // Folding a heading, which hides everything under it and moves every top
  // below, then unfolding it.
  std::uint64_t foldRevision = 1;
  bool folded = false;
  const std::size_t foldedHeading = source.find("## Section 12");
  MICRONOTES_REQUIRE(foldedHeading != std::string::npos);
  options.folded = [&](const micronotes::doc::SourceBlock& block) {
    return folded && block.start == foldedHeading;
  };
  options.foldRevision = foldRevision;
  settle("fold predicate installed");
  folded = true;
  options.foldRevision = ++foldRevision;
  settle("heading folded");
  settle("idle frame while folded");
  folded = false;
  options.foldRevision = ++foldRevision;
  settle("heading unfolded");
  options.folded = nullptr;
  options.foldRevision = 0;

  // Breaking a table's delimiter row and putting it back. A table is the one
  // construct here whose kind is decided by a line other than its first, and it
  // swings a whole block between `Complex` and `Paragraph` without changing its
  // extent -- the shape of edit most likely to catch a reuse map out.
  const std::size_t delimiter = source.find("|:------|------:|");
  MICRONOTES_REQUIRE(delimiter != std::string::npos);
  source.replace(delimiter, 1, "x");
  ++revision;
  options.caretOffset = delimiter + 1;
  settle("table delimiter broken");
  source.replace(delimiter, 1, "|");
  ++revision;
  options.caretOffset = delimiter + 1;
  settle("table delimiter restored");

  // Resizing, which invalidates every cached block at once, and then typing
  // again at the new width so the next edit reuses keys built under it.
  for(const float width : {480.0f, 900.0f, 620.0f}) {
    options.width = width;
    settle("resized");
    source.insert(middle, 1, 'w');
    ++revision;
    options.caretOffset = middle + 1;
    settle("typing after a resize");
  }

  MICRONOTES_REQUIRE(checked > 30);
}

// The same claim as above, made by a machine instead of by hand.
//
// The scripted sequence covers the edits somebody thought of; the placement is
// patched in place now, so the interesting bugs are the ones that need a
// particular *pair* of consecutive updates -- an edit that shifts the block
// count, then a caret move above the shift, then a fold whose head is inside
// the block that moved. There are more such pairs than anyone will write out,
// so this walks a seeded random sequence through them and holds the same
// invariant after every single step: indistinguishable from a layout built from
// scratch under the same inputs.
//
// Seeded, so a failure is reproducible from the name of the test alone. The
// step number in the message is the index into that fixed sequence.
namespace {

// One seeded random walk of edits, asserted against a from-scratch layout after
// every step. Factored out so the test can run a few independent sequences: a
// single seed explores one path through the state machine, and the bugs this is
// built to catch are in the transitions, not in any one state.
void walkRandomEdits(std::uint64_t seed, int steps) {
  // xorshift64*, so the sequence is fixed by the seed and does not depend on
  // the standard library's distribution implementations.
  std::uint64_t state = seed;
  const auto next = [&state]() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1Dull;
  };
  const auto pick = [&next](std::size_t bound) {
    return bound == 0 ? std::size_t {0} : static_cast<std::size_t>(next() >> 11) % bound;
  };

  std::string source = sectionedFixture(6);
  DocumentLayout incremental;
  incremental.setMetrics(stubMetrics());

  std::uint64_t revision = 1;
  std::uint64_t foldRevision = 1;
  int foldMode = 0;
  LayoutOptions options;
  options.width = 620.0f;
  options.foldRevision = foldRevision;
  options.folded = [&foldMode](const micronotes::doc::SourceBlock& block) {
    if(foldMode == 0) return false;
    if(block.kind != BlockKind::Heading) return false;
    return foldMode == 1 || block.level == 2;
  };

  int step = 0;
  const auto settle = [&](bool stamped) {
    options.sourceRevision = stamped ? revision : 0;
    incremental.update(source, options);

    DocumentLayout fresh;
    fresh.setMetrics(stubMetrics());
    LayoutOptions freshOptions = options;
    freshOptions.sourceRevision = 0;
    freshOptions.foldRevision = 0;
    fresh.update(source, freshOptions);

    std::string why;
    const bool agree = layoutsAgree(incremental, fresh, &why);
    micronotes::tests::require(agree, "random edit sequence " + std::to_string(seed) +
                                        " diverged at step " + std::to_string(step) + ": " + why);
  };

  settle(true);

  // Snippets rather than random bytes: what stresses the incremental path is an
  // edit that changes a block's *kind* or its extent, and those are markers. A
  // bare "```" is in the list deliberately -- an unclosed fence swallows the
  // rest of the document into one block, which is the largest structural change
  // a three-byte edit can make.
  static const char* kSnippets[] = {"x",  "hello ",   "**bold** ", "# ",     "- ",
                                    "> ", "`code` ",  "[[wiki]] ", "1. ",    "- [ ] ",
                                    "\n", "\n\n",     "```\n",     "|a|b|\n", "    "};
  constexpr std::size_t kSnippetCount = sizeof(kSnippets) / sizeof(kSnippets[0]);

  for(step = 1; step <= steps; ++step) {
    const std::size_t what = pick(20);
    if(what < 7) {
      const std::size_t at = pick(source.size() + 1);
      const std::string_view snippet = kSnippets[pick(kSnippetCount)];
      source.insert(at, snippet);
      ++revision;
      options.caretOffset = at + snippet.size();
    } else if(what < 11) {
      if(source.empty()) continue;
      const std::size_t at = pick(source.size());
      const std::size_t count = std::min<std::size_t>(pick(12) + 1, source.size() - at);
      source.erase(at, count);
      ++revision;
      options.caretOffset = at;
    } else if(what < 15) {
      options.caretOffset = pick(source.size() + 1);
    } else if(what < 16) {
      options.rawOffset = options.rawOffset == DocumentLayout::kNone
                            ? pick(source.size() + 1)
                            : DocumentLayout::kNone;
    } else if(what < 17) {
      foldMode = static_cast<int>(pick(3));
      options.foldRevision = ++foldRevision;
    } else if(what < 18) {
      options.width = 320.0f + static_cast<float>(pick(9)) * 90.0f;
    } else if(what < 19) {
      options.revealAll = !options.revealAll;
    }
    // The remaining draw changes nothing at all, which is the path that has to
    // answer "already correct" without touching the document.

    // Every fourth step drops the stamp, so the byte comparison that stands in
    // for it is exercised against a source the caller says nothing about.
    settle(step % 4 != 0);
  }
}

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
MICRONOTES_TEST(layout_folds_a_hand_wrapped_line_ending_into_one_space) {
  const auto drawnText = [](const std::string& source) {
    DocumentLayout layout;
    layout.setMetrics(stubMetrics());
    LayoutOptions options;
    options.width = 4000.0f;  // wide enough that nothing wraps on screen
    layout.update(source, options);
    std::string drawn;
    const auto& first = layout.layout(0);
    for(const auto& line : first.lines) {
      for(const auto& run : first.runsOf(line)) drawn += run.text;
    }
    return drawn;
  };

  MICRONOTES_REQUIRE(drawnText("A plain paragraph hand wrapped\n   across two source lines.\n") ==
                     "A plain paragraph hand wrapped across two source lines.");
  MICRONOTES_REQUIRE(drawnText("A **marked up** paragraph wrapped\n   across two source lines.\n") ==
                     "A marked up paragraph wrapped across two source lines.");
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
  options.caretOffset = at + 1;

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
  options.caretOffset = layout.blocks()[blocks / 4].start;
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

// A collapsed block gives up every one of its visual rows, so the prefix sum
// behind the row index is full of runs of repeated values. Stepping the caret
// down by one row has to cross a whole run of them in a single move -- which is
// the case a "find the block at this row" written as a scan gets right by
// accident and one written as a search only gets right deliberately.
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
  MICRONOTES_REQUIRE(layout.rowRelative(20, 1) == 20);

  // And it comes back whole on the next update.
  layout.update(source, options);
  MICRONOTES_REQUIRE(layout.totalHeight() > 0.0f);
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
