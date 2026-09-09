#include "TestSupport.h"

#include "doc/Flow.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using micronotes::doc::BlockLayout;
using micronotes::doc::Flow;
using micronotes::doc::FlowGeometry;
using micronotes::doc::FlowScratch;
using micronotes::doc::LineGroup;
using micronotes::doc::Metrics;
using micronotes::doc::RunStyle;
using micronotes::doc::TextRole;
using micronotes::doc::Token;

// The line breaker, driven directly.
//
// It used to be reachable only through `DocumentLayout::update`, which meant
// every wrapping rule below was tested -- when it was tested at all -- through
// a whole note, a block scan, an inline scan and a cache. The rules it owns are
// narrow and awkward (a cluster that must not break at a comma, a word wider
// than the column, spaces that belong to the line they ended) and each one is
// two tokens' worth of setup here.

namespace {

// One unit of width per character, so a column of 10 fits exactly ten
// characters and every expectation below is countable by hand.
Metrics unitMetrics() {
  Metrics metrics;
  metrics.measure = [](std::string_view value, const RunStyle&) {
    return static_cast<float>(value.size());
  };
  metrics.lineHeight = [](const RunStyle&) { return 1.0f; };
  return metrics;
}

Token word(std::string text, std::size_t start) {
  Token token;
  token.start = start;
  token.end = start + text.size();
  token.text = std::move(text);
  token.style = RunStyle {};
  token.role = TextRole::Body;
  return token;
}

Token space(std::size_t start, bool lineBreak = false) {
  Token token = word(" ", start);
  token.space = true;
  token.lineBreak = lineBreak;
  return token;
}

FlowGeometry column(float width, bool wrap = true) {
  FlowGeometry geometry;
  geometry.width = width;
  geometry.lineHeight = 1.0f;
  geometry.wrap = wrap;
  return geometry;
}

// The text of each visual line, so an expectation reads as the shape on screen.
std::vector<std::string> linesOf(const BlockLayout& out) {
  std::vector<std::string> lines;
  for(const auto& line : out.lines) {
    std::string text;
    for(const auto& run : out.runsOf(line)) text += run.text;
    lines.push_back(text);
  }
  return lines;
}

// Flows one group of tokens through a column of `width`.
BlockLayout flowOne(std::vector<Token> tokens, float width, bool wrap = true) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(std::move(tokens));
  Flow flow(metrics, column(width, wrap), out, scratch);
  flow.run(groups, groups.size());
  return out;
}

std::vector<Token> sentence(const std::vector<std::string>& words) {
  std::vector<Token> tokens;
  std::size_t at = 0;
  for(std::size_t i = 0; i < words.size(); ++i) {
    if(i > 0) {
      tokens.push_back(space(at));
      at += 1;
    }
    tokens.push_back(word(words[i], at));
    at += words[i].size();
  }
  return tokens;
}

}

MICRONOTES_TEST(flow_breaks_between_words_that_do_not_fit) {
  const auto out = flowOne(sentence({"aaa", "bbb", "ccc"}), 8.0f);
  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 2);
  // "aaa bbb" is 7 wide and fits; adding " ccc" would reach 11. The space that
  // ended the line stays on it -- see `placeCluster`.
  MICRONOTES_REQUIRE(lines[0] == "aaa bbb ");
  MICRONOTES_REQUIRE(lines[1] == "ccc");
}

MICRONOTES_TEST(flow_keeps_everything_on_one_line_when_wrap_is_off) {
  const auto out = flowOne(sentence({"aaa", "bbb", "ccc"}), 4.0f, /*wrap=*/false);
  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 1);
  MICRONOTES_REQUIRE(lines[0] == "aaa bbb ccc");
}

// The rule the cluster exists for. The tokenizer splits at every change of
// inline attribute, so `*emphasis*, code` arrives as two adjacent non-space
// tokens; breaking between them would open a line with a comma.
MICRONOTES_TEST(flow_does_not_break_between_adjacent_non_space_tokens) {
  std::vector<Token> tokens;
  tokens.push_back(word("aaaa", 0));
  tokens.push_back(space(4));
  tokens.push_back(word("emphasis", 5));  // one cluster with the comma below
  tokens.push_back(word(",", 13));
  const auto out = flowOne(std::move(tokens), 12.0f);
  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 2);
  MICRONOTES_REQUIRE(lines[0] == "aaaa ");
  // The comma stays with the word it follows rather than opening line two.
  MICRONOTES_REQUIRE(lines[1] == "emphasis,");
}

// A cluster wider than the column has to break inside itself -- between its
// tokens where it can.
MICRONOTES_TEST(flow_breaks_an_over_wide_cluster_between_its_tokens) {
  std::vector<Token> tokens;
  tokens.push_back(word("aaaaa", 0));
  tokens.push_back(word("bbbbb", 5));
  const auto out = flowOne(std::move(tokens), 6.0f);
  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 2);
  MICRONOTES_REQUIRE(lines[0] == "aaaaa");
  MICRONOTES_REQUIRE(lines[1] == "bbbbb");
}

// And mid-word where even one token does not fit, so no text is ever left
// past the right edge with no way to reach it.
MICRONOTES_TEST(flow_splits_a_word_wider_than_the_whole_column) {
  const auto out = flowOne({word("abcdefgh", 0)}, 3.0f);
  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 3);
  MICRONOTES_REQUIRE(lines[0] == "abc");
  MICRONOTES_REQUIRE(lines[1] == "def");
  MICRONOTES_REQUIRE(lines[2] == "gh");
}

// A split lands on codepoint boundaries, not bytes: half a UTF-8 sequence is
// not a character and would draw as a replacement glyph.
MICRONOTES_TEST(flow_splits_a_long_word_on_codepoint_boundaries) {
  // Four 2-byte codepoints. Measured by bytes, so the column of 3 admits one
  // codepoint per line rather than one and a half.
  const auto out = flowOne({word("\xc3\xa1\xc3\xa9\xc3\xad", 0)}, 3.0f);
  for(const auto& line : linesOf(out)) {
    // Every piece is a whole number of 2-byte codepoints.
    MICRONOTES_REQUIRE(line.size() % 2 == 0);
    MICRONOTES_REQUIRE(!line.empty());
  }
}

// A hard break in the source ends the line, but only once a word follows: a
// trailing newline is the block's own terminator and must not leave an empty
// line under it.
MICRONOTES_TEST(flow_applies_a_hard_break_only_when_a_word_follows) {
  std::vector<Token> tokens;
  tokens.push_back(word("aaa", 0));
  tokens.push_back(space(3, /*lineBreak=*/true));
  tokens.push_back(word("bbb", 4));
  const auto broken = linesOf(flowOne(std::move(tokens), 40.0f));
  MICRONOTES_REQUIRE(broken.size() == 2);
  MICRONOTES_REQUIRE(broken[0] == "aaa ");
  MICRONOTES_REQUIRE(broken[1] == "bbb");

  std::vector<Token> trailing;
  trailing.push_back(word("aaa", 0));
  trailing.push_back(space(3, /*lineBreak=*/true));
  const auto ended = linesOf(flowOne(std::move(trailing), 40.0f));
  MICRONOTES_REQUIRE(ended.size() == 1);
}

// `continuation` is what tells the view a break was forced by the column
// rather than written in the file, so a wrapped line is never mistaken for a
// new one. Only the line *after* a forced break carries it.
MICRONOTES_TEST(flow_marks_only_the_lines_a_column_break_continued) {
  const auto wrapped = flowOne(sentence({"aaa", "bbb"}), 4.0f);
  MICRONOTES_REQUIRE(wrapped.lines.size() == 2);
  MICRONOTES_REQUIRE(!wrapped.lines[0].continuation);
  MICRONOTES_REQUIRE(wrapped.lines[1].continuation);

  std::vector<Token> hard;
  hard.push_back(word("aaa", 0));
  hard.push_back(space(3, /*lineBreak=*/true));
  hard.push_back(word("bbb", 4));
  const auto out = flowOne(std::move(hard), 40.0f);
  MICRONOTES_REQUIRE(out.lines.size() == 2);
  // A break the writer put there: line two is not a continuation of line one.
  MICRONOTES_REQUIRE(!out.lines[1].continuation);
}

// Runs carry offsets relative to the block, so a block can be re-placed
// without re-shaping. The base is subtracted exactly once.
MICRONOTES_TEST(flow_records_run_offsets_relative_to_the_block) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(sentence({"aaa", "bbb"}));
  FlowGeometry geometry = column(40.0f);
  geometry.base = 100;
  // The tokens were built at offsets 0..7; shift them to sit at the base.
  for(auto& token : groups[0]) {
    token.start += 100;
    token.end += 100;
  }
  Flow flow(metrics, geometry, out, scratch);
  flow.run(groups, groups.size());

  MICRONOTES_REQUIRE(!out.runs.empty());
  MICRONOTES_REQUIRE(out.runs.front().srcStart == 0);
  MICRONOTES_REQUIRE(out.runs.back().srcEnd == 7);
}

// A hidden marker measures zero and still takes its place in the cluster, so
// the source offset it anchors stays with the word it belongs to.
MICRONOTES_TEST(flow_emits_a_hidden_token_as_a_zero_width_run_with_no_text) {
  std::vector<Token> tokens;
  Token marker = word("**", 0);
  marker.hidden = true;
  marker.isMarker = true;
  tokens.push_back(std::move(marker));
  tokens.push_back(word("bold", 2));
  const auto out = flowOne(std::move(tokens), 40.0f);

  MICRONOTES_REQUIRE(out.runs.size() == 2);
  MICRONOTES_REQUIRE(out.runs[0].rect.w == 0.0f);
  MICRONOTES_REQUIRE(out.runs[0].text.empty());
  MICRONOTES_REQUIRE(out.runs[0].isMarker);
  // The word that follows starts where the marker did, not two glyphs in.
  MICRONOTES_REQUIRE(out.runs[1].rect.x == 0.0f);
  MICRONOTES_REQUIRE(out.runs[1].text == "bold");
}

// The scratch buffers are borrowed, and a second block must not inherit the
// first one's half-built line. The flow clears them on construction, which is
// what lets one `FlowScratch` serve a whole document.
MICRONOTES_TEST(flow_reuses_borrowed_scratch_without_carrying_state_over) {
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();

  BlockLayout first;
  std::vector<LineGroup> groupsA;
  groupsA.push_back(sentence({"aaa", "bbb", "ccc"}));
  Flow(metrics, column(8.0f), first, scratch).run(groupsA, groupsA.size());

  BlockLayout second;
  std::vector<LineGroup> groupsB;
  groupsB.push_back(sentence({"aaa", "bbb", "ccc"}));
  Flow(metrics, column(8.0f), second, scratch).run(groupsB, groupsB.size());

  MICRONOTES_REQUIRE(linesOf(first) == linesOf(second));
}

// Each group is a line of the file: a group boundary closes the line whatever
// is on it, and the next group starts at the left edge.
MICRONOTES_TEST(flow_starts_a_new_line_for_every_group) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(sentence({"aaa"}));
  groups.push_back(sentence({"bbb"}));
  Flow flow(metrics, column(40.0f), out, scratch);
  flow.run(groups, groups.size());

  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 2);
  MICRONOTES_REQUIRE(lines[0] == "aaa");
  MICRONOTES_REQUIRE(lines[1] == "bbb");
  MICRONOTES_REQUIRE(!out.lines[1].continuation);
}

// `count` is what the caller staged, not what the buffer holds: the groups
// vector keeps the previous block's storage past the live prefix so the tokens
// can be reused rather than freed, and flowing those again would emit a block
// that is not the one asked for.
MICRONOTES_TEST(flow_reads_only_the_staged_prefix_of_the_group_buffer) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(sentence({"live"}));
  groups.push_back(sentence({"stale"}));  // past the prefix: must not be read
  Flow flow(metrics, column(40.0f), out, scratch);
  flow.run(groups, 1);

  const auto lines = linesOf(out);
  MICRONOTES_REQUIRE(lines.size() == 1);
  MICRONOTES_REQUIRE(lines[0] == "live");
}

// The block's own top offset is where the first line goes, and every line
// after it advances by exactly the line height -- so a block tiles.
MICRONOTES_TEST(flow_places_the_first_line_at_the_given_top_and_tiles_from_there) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(sentence({"aaa", "bbb", "ccc"}));
  FlowGeometry geometry = column(8.0f);
  geometry.top = 5.0f;
  geometry.lineHeight = 2.0f;
  Flow flow(metrics, geometry, out, scratch);
  flow.run(groups, groups.size());

  MICRONOTES_REQUIRE(out.lines.size() == 2);
  MICRONOTES_REQUIRE(out.lines[0].y == 5.0f);
  MICRONOTES_REQUIRE(out.lines[1].y == 7.0f);
  MICRONOTES_REQUIRE(flow.bottom() == 9.0f);
}

// Text starts at the column's left edge, and a wrapped line returns to it.
MICRONOTES_TEST(flow_indents_every_line_to_the_text_left) {
  BlockLayout out;
  FlowScratch scratch;
  const Metrics metrics = unitMetrics();
  std::vector<LineGroup> groups;
  groups.push_back(sentence({"aaa", "bbb"}));
  FlowGeometry geometry = column(4.0f);
  geometry.textLeft = 12.0f;
  Flow flow(metrics, geometry, out, scratch);
  flow.run(groups, groups.size());

  MICRONOTES_REQUIRE(out.lines.size() == 2);
  MICRONOTES_REQUIRE(out.runsOf(out.lines[0])[0].rect.x == 12.0f);
  MICRONOTES_REQUIRE(out.runsOf(out.lines[1])[0].rect.x == 12.0f);
}
