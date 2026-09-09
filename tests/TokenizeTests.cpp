#include "TestSupport.h"

#include "doc/Tokenize.h"

#include <string>
#include <string_view>
#include <vector>

using micronotes::doc::Attr;
using micronotes::doc::appendContentTokens;
using micronotes::doc::appendPlainTokens;
using micronotes::doc::displayText;
using micronotes::doc::LineGroup;
using micronotes::doc::RunStyle;
using micronotes::doc::TextRole;

// The tokenizer, driven directly: source bytes in, tokens out.
//
// It pairs with `FlowTests.cpp` -- these are the two halves of laying a block's
// text out, and both were reachable only through a whole `DocumentLayout`
// until they were separated. The rules here are all about staying
// byte-aligned with the source, which is what makes a click map back to an
// offset, and none of them is obvious from reading a token list.

namespace {

// The tokens' texts, so an expectation reads as what will be drawn. A hidden
// token has no text and shows as "<hidden>" so the two are distinguishable.
std::vector<std::string> textsOf(const LineGroup& group) {
  std::vector<std::string> out;
  for(const auto& token : group) out.push_back(token.hidden ? "<hidden>" : token.text);
  return out;
}

LineGroup plain(std::string_view source) {
  LineGroup out;
  appendPlainTokens(source, 0, source.size(), RunStyle {}, out);
  return out;
}

}

// Newlines and tabs become one space each so a run's text stays byte-aligned
// with the source it came from -- which is what lets prefix measurement map a
// pixel back to an offset.
MICRONOTES_TEST(tokenize_display_text_keeps_the_byte_count_of_its_source) {
  MICRONOTES_REQUIRE(displayText("a\nb") == "a b");
  MICRONOTES_REQUIRE(displayText("a\tb") == "a b");
  MICRONOTES_REQUIRE(displayText("a\r\nb") == "a  b");
  MICRONOTES_REQUIRE(displayText("plain").size() == 5);
  // Every substitution is one byte for one byte.
  const std::string source = "a\n\t\rb";
  MICRONOTES_REQUIRE(displayText(source).size() == source.size());
}

MICRONOTES_TEST(tokenize_splits_words_from_the_whitespace_between_them) {
  MICRONOTES_REQUIRE(textsOf(plain("one two")) == std::vector<std::string>({"one", " ", "two"}));
  // A run of spaces is one token, not several: it is one break opportunity.
  MICRONOTES_REQUIRE(textsOf(plain("one   two")) == std::vector<std::string>({"one", "   ", "two"}));
  MICRONOTES_REQUIRE(textsOf(plain("")).empty());
  MICRONOTES_REQUIRE(textsOf(plain("solo")) == std::vector<std::string>({"solo"}));
}

// A space token is marked as one, which is the only place the flow may break.
MICRONOTES_TEST(tokenize_marks_whitespace_tokens_as_spaces) {
  const auto tokens = plain("one two");
  MICRONOTES_REQUIRE(tokens.size() == 3);
  MICRONOTES_REQUIRE(!tokens[0].space);
  MICRONOTES_REQUIRE(tokens[1].space);
  MICRONOTES_REQUIRE(!tokens[2].space);
}

// Tokens carry the source offsets they came from, contiguously and with no
// gaps -- the property every click-to-offset query downstream depends on.
MICRONOTES_TEST(tokenize_covers_its_range_contiguously) {
  const std::string source = "alpha  beta\ngamma";
  const auto tokens = plain(source);
  MICRONOTES_REQUIRE(!tokens.empty());
  MICRONOTES_REQUIRE(tokens.front().start == 0);
  MICRONOTES_REQUIRE(tokens.back().end == source.size());
  for(std::size_t i = 1; i < tokens.size(); ++i) {
    MICRONOTES_REQUIRE(tokens[i].start == tokens[i - 1].end);
  }
}

// A line ending inside a block takes however many bytes it took -- the newline
// plus the indentation of the line continuing it -- and all but the last are
// emitted hidden. Hidden is zero width and still addressable, so the source
// stays byte-aligned with what is drawn, and only the final byte carries the
// break.
MICRONOTES_TEST(tokenize_hides_all_but_the_last_byte_of_a_multi_byte_line_ending) {
  const auto tokens = plain("a\n   b");
  MICRONOTES_REQUIRE(textsOf(tokens) == std::vector<std::string>({"a", "<hidden>", " ", "b"}));
  // The whitespace run is bytes 1..5; the first three are hidden and the last
  // one is the break.
  MICRONOTES_REQUIRE(tokens[1].start == 1);
  MICRONOTES_REQUIRE(tokens[1].end == 4);
  MICRONOTES_REQUIRE(!tokens[1].lineBreak);
  MICRONOTES_REQUIRE(tokens[2].start == 4);
  MICRONOTES_REQUIRE(tokens[2].end == 5);
  MICRONOTES_REQUIRE(tokens[2].lineBreak);
}

// A one-byte line ending is not split: there is nothing to hide.
MICRONOTES_TEST(tokenize_emits_a_lone_newline_as_one_breaking_space) {
  const auto tokens = plain("a\nb");
  MICRONOTES_REQUIRE(textsOf(tokens) == std::vector<std::string>({"a", " ", "b"}));
  MICRONOTES_REQUIRE(tokens[1].lineBreak);
}

// Whitespace with no newline in it is a break opportunity but not a break.
MICRONOTES_TEST(tokenize_does_not_break_on_spaces_without_a_newline) {
  const auto tokens = plain("a  b");
  MICRONOTES_REQUIRE(tokens[1].space);
  MICRONOTES_REQUIRE(!tokens[1].lineBreak);
}

// The general form splits at every change of inline attribute as well as at
// every space -- which is why `Flow` has to buffer a cluster before deciding
// where to break: `bold` and the comma after it arrive as two tokens with
// nothing between them.
MICRONOTES_TEST(tokenize_splits_at_every_change_of_inline_attribute) {
  const std::string source = "ab,";
  std::vector<Attr> attrs(source.size());
  attrs[0].strong = true;
  attrs[1].strong = true;
  // The comma carries no attribute, so it is a token of its own with no space
  // before it.
  LineGroup out;
  appendContentTokens(source, 0, source.size(), attrs, RunStyle {}, 12.0f, false, out);
  MICRONOTES_REQUIRE(textsOf(out) == std::vector<std::string>({"ab", ","}));
  MICRONOTES_REQUIRE(out[0].style.strong);
  MICRONOTES_REQUIRE(!out[1].style.strong);
  MICRONOTES_REQUIRE(!out[0].space && !out[1].space);
}

// A marker is hidden unless the block is revealed, and hidden means "no text,
// but still holding its offsets" -- that is what keeps the caret able to land
// between the asterisks and the word.
MICRONOTES_TEST(tokenize_hides_markers_unless_the_block_is_revealed) {
  const std::string source = "**x**";
  std::vector<Attr> attrs(source.size());
  for(std::size_t i : {0u, 1u, 3u, 4u}) attrs[i].marker = true;

  LineGroup hidden;
  appendContentTokens(source, 0, source.size(), attrs, RunStyle {}, 12.0f, /*revealed=*/false, hidden);
  MICRONOTES_REQUIRE(textsOf(hidden) == std::vector<std::string>({"<hidden>", "x", "<hidden>"}));
  // Offsets are still complete and contiguous across the hidden runs.
  MICRONOTES_REQUIRE(hidden.front().start == 0);
  MICRONOTES_REQUIRE(hidden.back().end == source.size());

  LineGroup shown;
  appendContentTokens(source, 0, source.size(), attrs, RunStyle {}, 12.0f, /*revealed=*/true, shown);
  MICRONOTES_REQUIRE(textsOf(shown) == std::vector<std::string>({"**", "x", "**"}));
  MICRONOTES_REQUIRE(shown[0].isMarker);
}

// A marker never carries the link index, revealed or not: clicking the
// asterisks of a link's label must not follow it.
MICRONOTES_TEST(tokenize_never_gives_a_marker_the_link_it_marks) {
  const std::string source = "[x](y)";
  std::vector<Attr> attrs(source.size());
  for(auto& attr : attrs) attr.link = 7;
  attrs[0].marker = true;
  LineGroup out;
  appendContentTokens(source, 0, source.size(), attrs, RunStyle {}, 12.0f, /*revealed=*/true, out);
  MICRONOTES_REQUIRE(!out.empty());
  MICRONOTES_REQUIRE(out.front().isMarker);
  MICRONOTES_REQUIRE(out.front().link == -1);
}

// Both forms append rather than replace, because a block is staged into one
// buffer the layout owns and reuses across blocks.
MICRONOTES_TEST(tokenize_appends_to_whatever_the_caller_already_staged) {
  LineGroup out;
  appendPlainTokens("one", 0, 3, RunStyle {}, out);
  const std::size_t after = out.size();
  appendPlainTokens("two", 0, 3, RunStyle {}, out);
  MICRONOTES_REQUIRE(out.size() == after + 1);
  MICRONOTES_REQUIRE(textsOf(out) == std::vector<std::string>({"one", "two"}));
}

// The plain path exists only as a shortcut for a block with no markup in it,
// so it must agree with the general one where they overlap: same texts, same
// offsets, same breaks. They are kept adjacent in the header for this reason,
// and this is what says they stayed in step.
MICRONOTES_TEST(tokenize_plain_path_agrees_with_the_general_one_on_unmarked_text) {
  for(const std::string source : {"one two", "a\n   b", "a\nb", "trailing  ", "  leading"}) {
    LineGroup viaPlain;
    appendPlainTokens(source, 0, source.size(), RunStyle {}, viaPlain);
    LineGroup viaContent;
    const std::vector<Attr> attrs(source.size());
    appendContentTokens(source, 0, source.size(), attrs, RunStyle {}, 12.0f, false, viaContent);

    micronotes::tests::require(textsOf(viaPlain) == textsOf(viaContent),
                               "the plain and general tokenizers disagree on: " + source);
    micronotes::tests::require(viaPlain.size() == viaContent.size(),
                               "different token counts for: " + source);
    for(std::size_t i = 0; i < viaPlain.size(); ++i) {
      micronotes::tests::require(viaPlain[i].start == viaContent[i].start &&
                                   viaPlain[i].end == viaContent[i].end,
                                 "different offsets for: " + source);
      micronotes::tests::require(viaPlain[i].lineBreak == viaContent[i].lineBreak,
                                 "different line breaks for: " + source);
      micronotes::tests::require(viaPlain[i].space == viaContent[i].space,
                                 "different space flags for: " + source);
    }
  }
}
