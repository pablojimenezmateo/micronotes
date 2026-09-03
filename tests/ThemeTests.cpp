#include "TestSupport.h"

#include "ui/Theme.h"

using micronotes::ui::calloutLabel;
using micronotes::ui::calloutStyle;
using micronotes::ui::setThemeMode;
using micronotes::ui::ThemeMode;

// The two scanners hand `calloutLabel` different bytes for the same callout:
// `doc::BlockScan` keeps `[!WARNING]` as written, md4c lower-cases it. The
// label is normalised so that cannot show.
MICRONOTES_TEST(callout_label_reads_the_same_whatever_case_reaches_it) {
  MICRONOTES_REQUIRE(calloutLabel("WARNING") == "Warning");
  MICRONOTES_REQUIRE(calloutLabel("warning") == "Warning");
  MICRONOTES_REQUIRE(calloutLabel("Warning") == "Warning");
  MICRONOTES_REQUIRE(calloutLabel("wArNiNg") == "Warning");
}

MICRONOTES_TEST(callout_label_names_an_untagged_callout) {
  // `> [!]` and a bare quote promoted to a callout have no kind, and the
  // default has to read like the others rather than like a placeholder.
  MICRONOTES_REQUIRE(calloutLabel("") == "Note");
}

// The colour was already normalised; this pins the pair together, since a
// label and a hue that disagree about which kind this is would be worse than
// either being wrong alone.
MICRONOTES_TEST(callout_style_matches_across_the_case_the_scanners_produce) {
  for(const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
    setThemeMode(mode);
    const auto upper = calloutStyle("CAUTION");
    const auto lower = calloutStyle("caution");
    MICRONOTES_REQUIRE(upper.accent.r == lower.accent.r);
    MICRONOTES_REQUIRE(upper.accent.g == lower.accent.g);
    MICRONOTES_REQUIRE(upper.accent.b == lower.accent.b);
    MICRONOTES_REQUIRE(upper.surface.r == lower.surface.r);
    // And a different kind is a different colour, or the tag says nothing.
    const auto note = calloutStyle("note");
    MICRONOTES_REQUIRE(!(note.accent.r == upper.accent.r && note.accent.g == upper.accent.g &&
                         note.accent.b == upper.accent.b));
  }
  setThemeMode(ThemeMode::Dark);
}
