#include "TestSupport.h"

#include "core/editor/WrappedLines.h"
#include "core/util/Utf8.h"

#include <string>
#include <string_view>

namespace {

using microcore::editor::WrappedLine;
using microcore::editor::wrapLines;

// Eight pixels per code point, so "how many characters fit" is just the width
// divided by eight and the expected offsets are readable.
int fakeWidth(std::string_view run) {
  return static_cast<int>(microcore::util::countCodePoints(run) * 8);
}

// Which line a caret at `offset` belongs to, using the same rule the editor's
// paint pass uses. Returns -1 when no line claims it, which is the bug the
// `next` field exists to prevent.
int lineForCaret(const std::vector<WrappedLine>& lines, std::size_t offset) {
  for(std::size_t i = 0; i < lines.size(); ++i) {
    if(offset >= lines[i].begin && offset < lines[i].next) return static_cast<int>(i);
  }
  return -1;
}

}

MICRONOTES_TEST(wrap_lines_preserves_the_source_exactly) {
  // The wrapper this replaces split on whitespace and re-joined with single
  // spaces, so an editor using it displayed text the user had not typed.
  const std::string source = "two  spaces\tand a tab";
  const auto lines = wrapLines(source, 4000.0f, fakeWidth);
  MICRONOTES_REQUIRE(lines.size() == 1);
  MICRONOTES_REQUIRE(source.substr(lines[0].begin, lines[0].end - lines[0].begin) == source);
}

MICRONOTES_TEST(wrap_lines_breaks_at_word_boundaries) {
  // 80px fits ten characters, so no two of these words share a line.
  const std::string source = "alpha bravo charlie";
  const auto lines = wrapLines(source, 80.0f, fakeWidth);
  MICRONOTES_REQUIRE(lines.size() == 3);
  MICRONOTES_REQUIRE(source.substr(lines[0].begin, lines[0].end - lines[0].begin) == "alpha");
  MICRONOTES_REQUIRE(source.substr(lines[1].begin, lines[1].end - lines[1].begin) == "bravo");
  MICRONOTES_REQUIRE(source.substr(lines[2].begin, lines[2].end - lines[2].begin) == "charlie");

  // Two short words do share one.
  const std::string pair = "ab cd ef";
  const auto packed = wrapLines(pair, 80.0f, fakeWidth);
  MICRONOTES_REQUIRE(packed.size() == 1);
}

MICRONOTES_TEST(wrap_lines_keeps_an_overlong_word_on_its_own_line) {
  // Breaking before a word that is wider than the line with nothing in front of
  // it would loop forever, or drop the word.
  const std::string source = "supercalifragilistic x";
  const auto lines = wrapLines(source, 40.0f, fakeWidth);
  MICRONOTES_REQUIRE(lines.size() == 2);
  MICRONOTES_REQUIRE(source.substr(lines[0].begin, lines[0].end - lines[0].begin) == "supercalifragilistic");
}

MICRONOTES_TEST(wrap_lines_starts_a_new_line_at_every_newline) {
  const std::string source = "one\ntwo\n\nfour";
  const auto lines = wrapLines(source, 4000.0f, fakeWidth);
  MICRONOTES_REQUIRE(lines.size() == 4);
  MICRONOTES_REQUIRE(source.substr(lines[2].begin, lines[2].end - lines[2].begin).empty());
  MICRONOTES_REQUIRE(source.substr(lines[3].begin, lines[3].end - lines[3].begin) == "four");
}

MICRONOTES_TEST(wrap_lines_claims_every_caret_offset) {
  // Every position the caret can occupy must belong to exactly one line,
  // including one parked in the whitespace a wrap consumed -- otherwise the
  // caret simply is not drawn while it sits there.
  const std::string source = "alpha bravo charlie\nsecond  paragraph";
  const auto lines = wrapLines(source, 80.0f, fakeWidth);
  for(std::size_t offset = 0; offset <= source.size(); ++offset) {
    micronotes::tests::require(lineForCaret(lines, offset) >= 0,
                               "no wrapped line claims caret offset " + std::to_string(offset));
  }
}

MICRONOTES_TEST(wrap_lines_puts_the_end_of_the_buffer_on_the_last_line) {
  const std::string source = "alpha bravo";
  const auto lines = wrapLines(source, 4000.0f, fakeWidth);
  MICRONOTES_REQUIRE(lineForCaret(lines, source.size()) == static_cast<int>(lines.size()) - 1);
}

MICRONOTES_TEST(wrap_lines_handles_empty_text) {
  const auto lines = wrapLines("", 200.0f, fakeWidth);
  MICRONOTES_REQUIRE(lines.size() == 1);
  MICRONOTES_REQUIRE(lines[0].begin == 0 && lines[0].end == 0);
  MICRONOTES_REQUIRE(lineForCaret(lines, 0) == 0);
}
