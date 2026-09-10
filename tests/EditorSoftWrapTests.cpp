#include "CoreAliases.h"
#include "TestSupport.h"

#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"

#include <cstdint>

// `core/editor/SoftWrap.h`: breaking a logical line into visual ones without
// changing a single source offset, which is what lets a click map back.

MICRONOTES_TEST(editor_soft_wraps_by_words_without_changing_source_offsets) {
  const std::string source = "alpha beta gamma";
  const auto rows = micronotes::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[0]) == "alpha beta ");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 11);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[1]) == "gamma");
  MICRONOTES_REQUIRE(rows[1].start == 11);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_keeps_remaining_words_together_when_they_fit) {
  const std::string source = "alpha beta gamma delta";
  const auto rows = micronotes::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[0]) == "alpha beta ");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 11);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[1]) == "gamma delta");
  MICRONOTES_REQUIRE(rows[1].start == 11);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_preserves_hard_newlines) {
  const std::string source = "one two\nthree";
  const auto rows = micronotes::editor::softWrap(source, 20, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[0]) == "one two");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 7);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[1]) == "three");
  MICRONOTES_REQUIRE(rows[1].start == 8);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_splits_oversized_words) {
  const std::string source = "abcdefgh";
  const auto rows = micronotes::editor::softWrap(source, 3, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 3);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[0]) == "abc");
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[1]) == "def");
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[2]) == "gh");
}

MICRONOTES_TEST(editor_soft_wrap_keeps_utf8_codepoints_intact) {
  const std::string source = "a\xC3\xA9\xC3\xA9";  // "aéé", each 'é' is two bytes
  const auto rows = micronotes::editor::softWrap(source, 2, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 3);
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[0]) == "a");
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[1]) == "\xC3\xA9");
  MICRONOTES_REQUIRE(micronotes::editor::textIn(source, rows[2]) == "\xC3\xA9");
}

MICRONOTES_TEST(editor_soft_wrap_maps_offsets_and_hit_testing) {
  const std::string source = "alpha beta gamma";
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  const auto rows = micronotes::editor::softWrap(source, 11, measure);
  MICRONOTES_REQUIRE(micronotes::editor::rowForOffset(rows, 0) == 0);
  MICRONOTES_REQUIRE(micronotes::editor::rowForOffset(rows, 12) == 1);
  MICRONOTES_REQUIRE(micronotes::editor::offsetForRowX(source, rows[1], 2.0f, measure) == 13);
}
