#include "TestSupport.h"

#include "library/Metadata.h"
#include "ui/Fuzzy.h"

using micronotes::ui::fuzzyScore;

MICRONOTES_TEST(fuzzy_matches_subsequences_and_rejects_missing_characters) {
  MICRONOTES_REQUIRE(fuzzyScore("Product roadmap", "prod").has_value());
  MICRONOTES_REQUIRE(fuzzyScore("Product roadmap", "pdmap").has_value());
  MICRONOTES_REQUIRE(!fuzzyScore("Product roadmap", "zzz").has_value());
  // Every query character must appear, in order.
  MICRONOTES_REQUIRE(!fuzzyScore("Product roadmap", "mapp").has_value());
}

MICRONOTES_TEST(fuzzy_is_case_insensitive) {
  MICRONOTES_REQUIRE(fuzzyScore("Meeting Notes", "MEETING").has_value());
  MICRONOTES_REQUIRE(fuzzyScore("Meeting Notes", "notes").has_value());
}

MICRONOTES_TEST(fuzzy_empty_query_matches_everything) {
  MICRONOTES_REQUIRE(fuzzyScore("anything", "").has_value());
}

MICRONOTES_TEST(fuzzy_prefers_word_boundaries_and_runs) {
  // "rm" as two word-initials should beat the same letters buried mid-word.
  const auto boundary = fuzzyScore("Roadmap Meeting", "rm");
  const auto buried = fuzzyScore("Barometer", "rm");
  MICRONOTES_REQUIRE(boundary.has_value());
  MICRONOTES_REQUIRE(buried.has_value());
  MICRONOTES_REQUIRE(*boundary > *buried);
}

MICRONOTES_TEST(fuzzy_prefers_consecutive_matches) {
  // Same length, and neither candidate offers a word boundary to score on, so
  // the consecutive run is the only thing separating them.
  const auto tight = fuzzyScore("zzreadzz", "read");
  const auto loose = fuzzyScore("zrzezazd", "read");
  MICRONOTES_REQUIRE(tight.has_value());
  MICRONOTES_REQUIRE(loose.has_value());
  MICRONOTES_REQUIRE(*tight > *loose);
}

MICRONOTES_TEST(fuzzy_treats_separators_as_word_boundaries) {
  // Segment starts in paths and hyphenated titles are a strong signal, which is
  // why a boundary hit can outweigh a longer consecutive run elsewhere.
  MICRONOTES_REQUIRE(fuzzyScore("work/meeting-notes", "wmn").has_value());
  MICRONOTES_REQUIRE(!fuzzyScore("work/meeting-notes", "wnm").has_value());
}

MICRONOTES_TEST(fallback_note_id_is_stable_distinct_and_filename_safe) {
  using micronotes::library::fallbackNoteId;
  const auto first = fallbackNoteId("Work/Meeting notes.md");
  MICRONOTES_REQUIRE(first == fallbackNoteId("Work/Meeting notes.md"));
  MICRONOTES_REQUIRE(first != fallbackNoteId("Personal/Meeting notes.md"));
  MICRONOTES_REQUIRE(!first.empty());
  // Used as a SQLite key and as a recovery file name, so it must stay opaque.
  for(const char c : first) {
    const bool safe = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z');
    MICRONOTES_REQUIRE(safe);
  }
}
