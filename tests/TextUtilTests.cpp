#include "TestSupport.h"

#include "ui/TextUtil.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

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

// --- search snippets ---------------------------------------------------
//
// A matching line in the sidebar is the reason its row is there, so the match
// is the one part of the line that cannot be cut away. These pin that: the
// answer always contains the match, and always reports where inside the
// returned text it ended up.

namespace {

using micronotes::ui::SnippetWindow;

// Every window has to name a range inside its own text, and that range has to
// be the query. Checked on every case rather than eyeballed per assertion,
// because a start that points past the end of the text is exactly the bug a
// highlight drawn from it would show as a stripe in the wrong place.
void requireMarksTheMatch(const SnippetWindow& window, std::string_view query) {
  MICRONOTES_REQUIRE(window.start <= window.text.size());
  MICRONOTES_REQUIRE(window.start + window.length <= window.text.size());
  MICRONOTES_REQUIRE(window.length == query.size());
  MICRONOTES_REQUIRE(std::string_view(window.text).substr(window.start, window.length) == query);
}

}

MICRONOTES_TEST(snippet_leaves_a_line_that_already_fits) {
  const auto window = micronotes::ui::snippetAroundMatch("a needle here", 2, 6, 200, measureEight);
  MICRONOTES_REQUIRE(window.text == "a needle here");
  requireMarksTheMatch(window, "needle");
}

// The match near the front: the head is worth keeping, so the tail goes, which
// is what an ordinary label truncation does.
MICRONOTES_TEST(snippet_trims_the_tail_when_the_match_is_near_the_front) {
  const std::string line = "a needle and then a great deal more text after it";
  const auto window = micronotes::ui::snippetAroundMatch(line, 2, 6, 160, measureEight);
  MICRONOTES_REQUIRE(measureEight(window.text) <= 160);
  MICRONOTES_REQUIRE(window.text.starts_with("a needle"));
  MICRONOTES_REQUIRE(window.text.ends_with("..."));
  requireMarksTheMatch(window, "needle");
}

// The match past the column's width: this is the case the sidebar used to get
// wrong, listing a note as matching and then showing a line with nothing
// marked on it because the ellipsis fell before the match.
MICRONOTES_TEST(snippet_trims_the_head_to_keep_a_late_match_in_view) {
  const std::string line = "a great deal of run-up before the needle finally appears";
  const auto at = line.find("needle");
  const auto window = micronotes::ui::snippetAroundMatch(line, at, 6, 160, measureEight);
  MICRONOTES_REQUIRE(measureEight(window.text) <= 160);
  MICRONOTES_REQUIRE(window.text.starts_with("..."));
  requireMarksTheMatch(window, "needle");
}

// A match at the very end of a long line still has to be shown.
MICRONOTES_TEST(snippet_keeps_a_match_at_the_end_of_the_line) {
  const std::string line = "text that runs on and on and on and ends with the needle";
  const auto at = line.find("needle");
  const auto window = micronotes::ui::snippetAroundMatch(line, at, 6, 120, measureEight);
  MICRONOTES_REQUIRE(measureEight(window.text) <= 120);
  requireMarksTheMatch(window, "needle");
}

MICRONOTES_TEST(snippet_handles_a_line_with_nothing_to_mark) {
  // A note whose title matched but whose text did not: no range, so the line is
  // shown and nothing is highlighted.
  const auto window = micronotes::ui::snippetAroundMatch("just a line", 0, 0, 200, measureEight);
  MICRONOTES_REQUIRE(window.text == "just a line");
  MICRONOTES_REQUIRE(window.length == 0);
}

// A range that does not fit the line is clamped rather than trusted: the offset
// comes from a search over a body the index read, and the line here is one row
// of it.
MICRONOTES_TEST(snippet_clamps_a_range_that_runs_past_the_line) {
  const auto window = micronotes::ui::snippetAroundMatch("short", 3, 40, 200, measureEight);
  MICRONOTES_REQUIRE(window.text == "short");
  MICRONOTES_REQUIRE(window.start == 3);
  MICRONOTES_REQUIRE(window.start + window.length <= window.text.size());

  const auto beyond = micronotes::ui::snippetAroundMatch("short", 99, 4, 200, measureEight);
  MICRONOTES_REQUIRE(beyond.start <= beyond.text.size());
  MICRONOTES_REQUIRE(beyond.length == 0);
}

MICRONOTES_TEST(snippet_survives_no_room_at_all) {
  const auto window = micronotes::ui::snippetAroundMatch("a needle", 2, 6, 0, measureEight);
  MICRONOTES_REQUIRE(window.text.empty());
  MICRONOTES_REQUIRE(window.length == 0);
}

// Trims land on code point boundaries, never inside a UTF-8 sequence: half a
// character is not text, and the renderer is handed this string as-is.
MICRONOTES_TEST(snippet_cuts_multibyte_text_at_a_code_point) {
  const std::string line = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9 needle \xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
  const auto at = line.find("needle");
  const auto window = micronotes::ui::snippetAroundMatch(line, at, 6, 96, measureEight);
  MICRONOTES_REQUIRE(measureEight(window.text) <= 96);
  requireMarksTheMatch(window, "needle");
  for(std::size_t i = 0; i < window.text.size();) {
    const auto lead = static_cast<unsigned char>(window.text[i]);
    const std::size_t width = lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    // Every continuation byte the lead promised is present, so no sequence was
    // cut in half.
    for(std::size_t j = 1; j < width; ++j) {
      MICRONOTES_REQUIRE(i + j < window.text.size());
      MICRONOTES_REQUIRE((static_cast<unsigned char>(window.text[i + j]) & 0xC0) == 0x80);
    }
    i += width;
  }
}

// --- the searches behind the trimming -----------------------------------
//
// All three trims estimate the answer from the full-string measurement they
// already took and then confirm it, rather than bisecting down from the whole
// string. The estimate is only ever an estimate -- a proportional font is not a
// fixed advance -- so what has to be pinned is that a wrong guess costs probes
// and never correctness. These compare against a linear reference over every
// code point boundary, under a measurer whose per-character widths vary by a
// factor of ten.

namespace {

// A deliberately lumpy stand-in: `i` is narrow, `W` is wide, and a multi-byte
// code point is wider still. An advance-per-byte estimate is wrong here in both
// directions, which is the point.
int measureLumpy(std::string_view value) {
  int width = 0;
  for(std::size_t i = 0; i < value.size(); ++i) {
    const auto byte = static_cast<unsigned char>(value[i]);
    if((byte & 0xC0) == 0x80) continue;
    if(byte >= 0x80) width += 22;
    else if(value[i] == 'i' || value[i] == 'l' || value[i] == '.') width += 3;
    else if(value[i] == 'W' || value[i] == 'M') width += 26;
    else if(value[i] == ' ') width += 5;
    else width += 11;
  }
  return width;
}

std::vector<std::size_t> boundaries(std::string_view value) {
  std::vector<std::size_t> stops;
  for(std::size_t i = 0; i <= value.size();) {
    stops.push_back(i);
    if(i == value.size()) break;
    ++i;
    while(i < value.size() && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80) ++i;
  }
  return stops;
}

// What `ellipsizeToFit` promises, worked out by trying every boundary.
std::string ellipsizeReference(std::string_view value, int maxWidth,
                               const std::function<int(std::string_view)>& measure) {
  if(maxWidth <= 0) return "";
  if(measure(value) <= maxWidth) return std::string(value);
  std::size_t best = 0;
  for(const std::size_t stop : boundaries(value)) {
    if(measure(std::string(value.substr(0, stop)) + "...") <= maxWidth) best = stop;
  }
  return std::string(value.substr(0, best)) + "...";
}

// ...and what `breakToFit` promises.
std::size_t breakReference(std::string_view value, int maxWidth,
                           const std::function<int(std::string_view)>& measure) {
  if(value.empty()) return 0;
  if(maxWidth <= 0 || measure(value) <= maxWidth) return value.size();
  const auto stops = boundaries(value);
  std::size_t best = stops.size() > 1 ? stops[1] : value.size();
  for(const std::size_t stop : stops) {
    if(stop > best && measure(value.substr(0, stop)) <= maxWidth) best = stop;
  }
  return best;
}

// The head cut `snippetAroundMatch` settles on: the smallest run-up that lets
// the head ellipsis, the match and the trailing ellipsis fit together.
std::size_t snippetHeadReference(std::string_view line, std::size_t matchStart, std::size_t matchLength,
                                 int maxWidth, const std::function<int(std::string_view)>& measure) {
  const std::size_t matchEnd = matchStart + matchLength;
  const auto survives = [&](std::size_t from) {
    std::string probe;
    if(from > 0) probe += "...";
    probe.append(line.substr(from, matchEnd - from));
    probe.append("...");
    return measure(probe) <= maxWidth;
  };
  const auto stops = boundaries(line);
  std::size_t atMatch = 0;
  for(const std::size_t stop : stops) {
    if(stop <= matchStart) atMatch = stop;
  }
  if(survives(0)) return 0;
  if(!survives(atMatch)) return atMatch;
  for(const std::size_t stop : stops) {
    if(stop <= atMatch && survives(stop)) return stop;
  }
  return atMatch;
}

const char* kLumpyLines[] = {
  "a needle and then a great deal more text after it",
  "WWWW MMMM iiii llll a needle WWWW MMMM iiii llll and more besides",
  "iiiiiiiiiiiiiiiiiiiiiiiiiiiiii needle",
  "\xc3\xa9\xc3\xa9\xc3\xa9 WW iii \xc3\xa9\xc3\xa9 needle \xc3\xa9\xc3\xa9\xc3\xa9 WW",
  "needle at the very front of a line that runs on for a while yet",
  "a line that runs on for a while yet and ends with the needle",
};

}

MICRONOTES_TEST(ellipsize_to_fit_matches_a_linear_reference_under_a_lumpy_font) {
  for(const char* line : kLumpyLines) {
    for(int width = -4; width <= 400; width += 3) {
      const std::string got = ellipsizeToFit(line, width, measureLumpy);
      const std::string want = ellipsizeReference(line, width, measureLumpy);
      micronotes::tests::require(got == want, std::string("ellipsize \"") + line + "\" at " +
                                                  std::to_string(width) + ": got \"" + got +
                                                  "\" want \"" + want + "\"");
    }
  }
}

MICRONOTES_TEST(break_to_fit_matches_a_linear_reference_under_a_lumpy_font) {
  for(const char* line : kLumpyLines) {
    for(int width = -4; width <= 400; width += 3) {
      const std::size_t got = breakToFit(line, width, measureLumpy);
      const std::size_t want = breakReference(line, width, measureLumpy);
      micronotes::tests::require(got == want, std::string("break \"") + line + "\" at " +
                                                  std::to_string(width) + ": got " +
                                                  std::to_string(got) + " want " + std::to_string(want));
    }
  }
}

// The snippet's head cut, checked through the window it produces: the text has
// to fit, the match has to be in it, and the run-up kept has to be the shortest
// one that works -- one boundary less and the trailing trim would eat the match.
MICRONOTES_TEST(snippet_head_cut_matches_a_linear_reference_under_a_lumpy_font) {
  for(const char* raw : kLumpyLines) {
    const std::string line = raw;
    const auto at = line.find("needle");
    if(at == std::string::npos) continue;
    for(int width = 1; width <= 400; width += 1) {
      const auto window = micronotes::ui::snippetAroundMatch(line, at, 6, width, measureLumpy);
      std::string want;
      if(measureLumpy(line) <= width) {
        want = line;
      } else {
        const std::size_t from = snippetHeadReference(line, at, 6, width, measureLumpy);
        if(from > 0) want += "...";
        want.append(line.substr(from));
        want = ellipsizeToFit(std::move(want), width, measureLumpy);
      }
      micronotes::tests::require(window.text == want,
                                 std::string("snippet at ") + std::to_string(width) + ": got \"" +
                                     window.text + "\" want \"" + want + "\"");
      micronotes::tests::require(measureLumpy(window.text) <= width || window.text == "...",
                                 "snippet overflowed its column at " + std::to_string(width));
    }
  }
}

// The number this was all about. One snippet was ~18 shaping passes because
// both searches bisected down from the whole line; the estimate turns that into
// a handful, and the handful must not creep back up.
MICRONOTES_TEST(snippet_measures_a_handful_of_times_not_eighteen) {
  const std::string line =
      "a great deal of run-up text before the needle finally appears, and then a "
      "great deal more text after it so that neither end of the line fits";
  const auto at = line.find("needle");
  int measures = 0;
  const auto counted = [&](std::string_view value) {
    ++measures;
    return measureEight(value);
  };
  const auto window = micronotes::ui::snippetAroundMatch(line, at, 6, 200, counted);
  requireMarksTheMatch(window, "needle");
  // Under a fixed advance the estimate is exact, so this is: the whole line,
  // the two ellipsis probes, the head search confirming its guess and its
  // neighbour, and the tail trim doing the same.
  micronotes::tests::require(measures <= 10, "snippet took " + std::to_string(measures) + " measurements");
}
