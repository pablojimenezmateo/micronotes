#include "TestSupport.h"

#include "ui/TextUtil.h"

#include <string>
#include <string_view>

using micronotes::ui::breakToFit;
using micronotes::ui::ellipsizeToFit;

namespace {

// A fixed-advance stand-in for a font: every code point is eight pixels wide.
// Deterministic, so a test can say exactly how many characters fit.
int measureEight(std::string_view value) {
  int glyphs = 0;
  for(const char c : value) {
    if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
  }
  return glyphs * 8;
}

}

MICRONOTES_TEST(ellipsize_to_fit_leaves_a_string_that_already_fits) {
  MICRONOTES_REQUIRE(ellipsizeToFit("abcd", 100, measureEight) == "abcd");
  MICRONOTES_REQUIRE(ellipsizeToFit("abcd", 32, measureEight) == "abcd");
}

MICRONOTES_TEST(ellipsize_to_fit_keeps_the_longest_prefix_that_fits) {
  // 10 characters at 8px is 80px. In 64px, eight characters fit, three of which
  // the ellipsis takes -- so five of the original survive.
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", 64, measureEight) == "abcde...");
  // One pixel short of the whole string. The ellipsis costs three characters of
  // room, so shortening by one character is never the answer: six survive.
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", 79, measureEight) == "abcdef...");
}

MICRONOTES_TEST(ellipsize_to_fit_falls_back_to_the_ellipsis_alone) {
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", 24, measureEight) == "...");
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", 8, measureEight) == "...");
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", 0, measureEight).empty());
  MICRONOTES_REQUIRE(ellipsizeToFit("abcdefghij", -5, measureEight).empty());
}

// The reason this exists. Truncating by bytes splits a multi-byte character and
// produces something that is not UTF-8, which the renderer draws as tofu or
// drops -- and a note titled in any language but English hits it immediately.
MICRONOTES_TEST(ellipsize_to_fit_never_cuts_a_character_in_half) {
  // Five two-byte code points, then five one-byte ones.
  const std::string value = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9" "abcde";
  const std::string out = ellipsizeToFit(value, 64, measureEight);
  MICRONOTES_REQUIRE(out == "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9...");

  // Every truncation of it, at every width, stays valid UTF-8: no byte of the
  // result may be a continuation byte that lost its lead byte.
  for(int width = 1; width <= 100; ++width) {
    const std::string shortened = ellipsizeToFit(value, width, measureEight);
    bool expectContinuation = false;
    for(const char c : shortened) {
      const auto byte = static_cast<unsigned char>(c);
      const bool continuation = (byte & 0xC0) == 0x80;
      MICRONOTES_REQUIRE(continuation == expectContinuation);
      expectContinuation = (byte & 0xE0) == 0xC0;
    }
    MICRONOTES_REQUIRE(!expectContinuation);
  }
}

// Bisection, not one pass per character removed: a long label used to cost a
// full shaping pass for every character it had to drop.
MICRONOTES_TEST(ellipsize_to_fit_measures_a_handful_of_times_not_once_per_character) {
  const std::string value(4000, 'x');
  int measures = 0;
  const auto counted = [&](std::string_view candidate) {
    ++measures;
    return measureEight(candidate);
  };
  const std::string out = ellipsizeToFit(value, 80, counted);
  MICRONOTES_REQUIRE(out == std::string(7, 'x') + "...");
  // log2(4001) is under 12, plus the initial "does the whole thing fit" pass.
  MICRONOTES_REQUIRE(measures <= 16);
}

MICRONOTES_TEST(break_to_fit_returns_the_whole_string_when_it_fits) {
  MICRONOTES_REQUIRE(breakToFit("abcd", 100, measureEight) == 4);
  MICRONOTES_REQUIRE(breakToFit("abcd", 32, measureEight) == 4);
  MICRONOTES_REQUIRE(breakToFit("", 32, measureEight) == 0);
}

MICRONOTES_TEST(break_to_fit_keeps_the_longest_prefix_that_fits) {
  // Eight pixels a code point, so ten of them fit in eighty.
  MICRONOTES_REQUIRE(breakToFit("abcdefghijklmno", 80, measureEight) == 10);
  MICRONOTES_REQUIRE(breakToFit("abcdefghijklmno", 81, measureEight) == 10);
  MICRONOTES_REQUIRE(breakToFit("abcdefghijklmno", 88, measureEight) == 11);
}

// The bug this replaced popped one *byte* at a time, so a cut could land inside
// a multi-byte sequence and hand the renderer bytes that are not text. Every
// answer here has to be a code point boundary, including when only part of a
// character's worth of room is left.
MICRONOTES_TEST(break_to_fit_never_cuts_inside_a_code_point) {
  const std::string value = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";  // five e-acutes
  MICRONOTES_REQUIRE(value.size() == 10);
  for(int width = -4; width <= 60; ++width) {
    const std::size_t cut = breakToFit(value, width, measureEight);
    micronotes::tests::require(cut % 2 == 0, "cut " + std::to_string(cut) + " at width " +
                                               std::to_string(width) + " split a code point");
    micronotes::tests::require(cut > 0, "cut nothing at width " + std::to_string(width));
    micronotes::tests::require(cut <= value.size(), "cut past the end");
  }
  // Two code points fit in 16 pixels and a third does not.
  MICRONOTES_REQUIRE(breakToFit(value, 16, measureEight) == 4);
  // Not even one fits in four, and one is still what comes back: it has to go
  // somewhere, and the caller's alternative is an empty line forever.
  MICRONOTES_REQUIRE(breakToFit(value, 4, measureEight) == 2);
}

MICRONOTES_TEST(break_to_fit_bisects_rather_than_walking) {
  const std::string value(4000, 'x');
  int measures = 0;
  const auto counted = [&](std::string_view part) {
    ++measures;
    return measureEight(part);
  };
  MICRONOTES_REQUIRE(breakToFit(value, 80, counted) == 10);
  // log2(4001) is under 12, plus the initial "does the whole thing fit" pass.
  MICRONOTES_REQUIRE(measures <= 16);
}
