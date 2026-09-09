#include "TestSupport.h"

#include "core/util/StringUtil.h"

#include <string>
#include <vector>

using microcore::util::equalsIgnoringAsciiCase;
using microcore::util::isAsciiLower;
using microcore::util::isAsciiSpace;
using microcore::util::isAsciiUpper;
using microcore::util::splitLines;
using microcore::util::toLowerAscii;
using microcore::util::toLowerAsciiInPlace;
using microcore::util::toUpperAscii;
using microcore::util::toUpperAsciiInPlace;
using microcore::util::trim;

// The five primitives the tree kept writing out by hand, pinned.
//
// The header exists because each had been duplicated and the copies had
// drifted -- three `trim`s that disagreed about carriage returns, eight
// spellings of the case fold. Until now none of them had a test of its own:
// the behaviour they are meant to standardise on was only ever asserted
// indirectly, through whichever caller happened to exercise it. That is the
// wrong way round for a file whose entire purpose is to be the one answer.

MICRONOTES_TEST(string_util_trim_takes_all_six_whitespace_bytes_off_both_ends) {
  MICRONOTES_REQUIRE(trim("  hello  ") == "hello");
  // The asymmetry that made one of the three old copies wrong: CR at the front
  // as well as the back.
  MICRONOTES_REQUIRE(trim("\r\n\t hello \t\r\n") == "hello");
  MICRONOTES_REQUIRE(trim("\v\fhello\v\f") == "hello");
  MICRONOTES_REQUIRE(trim("") == "");
  MICRONOTES_REQUIRE(trim("   ") == "");
  // Interior whitespace is not touched.
  MICRONOTES_REQUIRE(trim("  a  b  ") == "a  b");
}

MICRONOTES_TEST(string_util_is_ascii_space_names_six_bytes_and_no_others) {
  for(const char c : std::string(" \t\r\n\v\f")) MICRONOTES_REQUIRE(isAsciiSpace(c));
  MICRONOTES_REQUIRE(!isAsciiSpace('a'));
  MICRONOTES_REQUIRE(!isAsciiSpace('\0'));
  // A byte above 0x7F is not whitespace, and asking must not be undefined:
  // `isAsciiSpace` takes a `char`, which is signed here, so this is exactly the
  // input that makes `std::isspace` undefined without a cast.
  MICRONOTES_REQUIRE(!isAsciiSpace(static_cast<char>(0xA0)));
}

MICRONOTES_TEST(string_util_folds_ascii_letters_in_both_directions) {
  MICRONOTES_REQUIRE(toLowerAscii('A') == 'a');
  MICRONOTES_REQUIRE(toLowerAscii('z') == 'z');
  MICRONOTES_REQUIRE(toUpperAscii('a') == 'A');
  MICRONOTES_REQUIRE(toUpperAscii('Z') == 'Z');
  // Digits, punctuation and the byte either side of the letter ranges are
  // returned unchanged -- the off-by-one that a hand-rolled fold gets wrong.
  for(const char c : std::string("0123456789_-[]@`{}")) {
    MICRONOTES_REQUIRE(toLowerAscii(c) == c);
    MICRONOTES_REQUIRE(toUpperAscii(c) == c);
  }
  MICRONOTES_REQUIRE(isAsciiUpper('A') && isAsciiUpper('Z'));
  MICRONOTES_REQUIRE(!isAsciiUpper('@') && !isAsciiUpper('['));
  MICRONOTES_REQUIRE(isAsciiLower('a') && isAsciiLower('z'));
  MICRONOTES_REQUIRE(!isAsciiLower('`') && !isAsciiLower('{'));
}

// The reason the fold stops at ASCII. A byte above 0x7F is left alone, so a
// note's accented text survives a fold unchanged instead of depending on the
// user's locale.
MICRONOTES_TEST(string_util_leaves_non_ascii_bytes_alone) {
  // "ÁÉÍ" in UTF-8, and its lowercase form.
  const std::string upper = "\xc3\x81\xc3\x89\xc3\x8d";
  MICRONOTES_REQUIRE(toLowerAscii(upper) == upper);
  MICRONOTES_REQUIRE(toUpperAscii(upper) == upper);
  // Mixed: only the ASCII half moves.
  MICRONOTES_REQUIRE(toLowerAscii("A\xc3\x81Z") == "a\xc3\x81z");
  MICRONOTES_REQUIRE(toUpperAscii("a\xc3\x81z") == "A\xc3\x81Z");
}

MICRONOTES_TEST(string_util_folds_a_whole_string_in_place_and_by_value) {
  MICRONOTES_REQUIRE(toLowerAscii("MiXeD Case 42") == "mixed case 42");
  MICRONOTES_REQUIRE(toUpperAscii("MiXeD Case 42") == "MIXED CASE 42");
  std::string value = "MiXeD";
  toLowerAsciiInPlace(value);
  MICRONOTES_REQUIRE(value == "mixed");
  toUpperAsciiInPlace(value);
  MICRONOTES_REQUIRE(value == "MIXED");
  std::string empty;
  toLowerAsciiInPlace(empty);
  toUpperAsciiInPlace(empty);
  MICRONOTES_REQUIRE(empty.empty());
}

MICRONOTES_TEST(string_util_compares_ignoring_ascii_case_without_allocating) {
  MICRONOTES_REQUIRE(equalsIgnoringAsciiCase("Hello", "hELLO"));
  MICRONOTES_REQUIRE(equalsIgnoringAsciiCase("", ""));
  MICRONOTES_REQUIRE(!equalsIgnoringAsciiCase("hello", "hello "));
  MICRONOTES_REQUIRE(!equalsIgnoringAsciiCase("hello", "help"));
  // Length is checked first, so a prefix is not a match.
  MICRONOTES_REQUIRE(!equalsIgnoringAsciiCase("ab", "abc"));
}

// "The last line" is what a text editor means by it: a buffer ending in a
// newline has one more line than it has newlines, and that trailing empty
// piece is where a caret can sit.
MICRONOTES_TEST(string_util_split_lines_keeps_the_trailing_piece) {
  MICRONOTES_REQUIRE(splitLines("a\nb") == std::vector<std::string>({"a", "b"}));
  MICRONOTES_REQUIRE(splitLines("a\nb\n") == std::vector<std::string>({"a", "b", ""}));
  MICRONOTES_REQUIRE(splitLines("") == std::vector<std::string>({""}));
  MICRONOTES_REQUIRE(splitLines("\n") == std::vector<std::string>({"", ""}));
  MICRONOTES_REQUIRE(splitLines("one") == std::vector<std::string>({"one"}));
  // A CR is content, not a line ending: the split is on '\n' alone, so a CRLF
  // buffer keeps its CR and the trim above is what takes it off.
  MICRONOTES_REQUIRE(splitLines("a\r\nb") == std::vector<std::string>({"a\r", "b"}));
}
