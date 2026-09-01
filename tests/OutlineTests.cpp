#include "TestSupport.h"

#include "ui/Outline.h"

#include <limits>
#include <string>

using micronotes::ui::outlineEntryAt;
using micronotes::ui::outlineOf;

MICRONOTES_TEST(outline_lists_headings_in_order) {
  const auto entries = outlineOf("# One\n\ntext\n\n## Two\n\n### Three\n\n# Four\n");
  MICRONOTES_REQUIRE(entries.size() == 4);
  MICRONOTES_REQUIRE(entries[0].text == "One" && entries[0].level == 1);
  MICRONOTES_REQUIRE(entries[1].text == "Two" && entries[1].level == 2);
  MICRONOTES_REQUIRE(entries[2].text == "Three" && entries[2].level == 3);
  MICRONOTES_REQUIRE(entries[3].text == "Four" && entries[3].level == 1);
}

// Indentation follows the shape of the note rather than the raw level, so a
// note written entirely in h3 reads as a flat list.
MICRONOTES_TEST(outline_indents_by_shape_not_by_level) {
  const auto flat = outlineOf("### A\n\n### B\n\n### C\n");
  MICRONOTES_REQUIRE(flat.size() == 3);
  for(const auto& entry : flat) MICRONOTES_REQUIRE(entry.depth == 0);

  const auto nested = outlineOf("# A\n\n## B\n\n### C\n\n## D\n\n# E\n");
  MICRONOTES_REQUIRE(nested.size() == 5);
  MICRONOTES_REQUIRE(nested[0].depth == 0);
  MICRONOTES_REQUIRE(nested[1].depth == 1);
  MICRONOTES_REQUIRE(nested[2].depth == 2);
  // Back out to the h2's level, not to wherever the h3 left the stack.
  MICRONOTES_REQUIRE(nested[3].depth == 1);
  MICRONOTES_REQUIRE(nested[4].depth == 0);
}

// The two things that look like headings and are not.
MICRONOTES_TEST(outline_ignores_hashes_that_are_not_headings) {
  const auto fenced = outlineOf("# Real\n\n```\n# not a heading\n```\n\n## Also real\n");
  MICRONOTES_REQUIRE(fenced.size() == 2);
  MICRONOTES_REQUIRE(fenced[0].text == "Real");
  MICRONOTES_REQUIRE(fenced[1].text == "Also real");

  const auto midline = outlineOf("Some text # not a heading\n");
  MICRONOTES_REQUIRE(midline.empty());
}

MICRONOTES_TEST(outline_strips_closing_hashes) {
  const auto entries = outlineOf("## Balanced ##\n");
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries[0].text == "Balanced");
}

// A heading with nothing after the marker still needs a row, or the panel
// silently loses a section the note really has.
MICRONOTES_TEST(outline_names_an_empty_heading) {
  const auto entries = outlineOf("#\n");
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries[0].text == "Untitled section");
}

// The offset is where the caret goes, so it must land on the title text rather
// than on the marker before it.
MICRONOTES_TEST(outline_offset_points_at_the_heading_text) {
  const std::string source = "## Target\n";
  const auto entries = outlineOf(source);
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(source.compare(entries[0].offset, 6, "Target") == 0);
}

MICRONOTES_TEST(outline_marks_where_the_caret_is) {
  const std::string source = "# One\n\nbody\n\n# Two\n\nmore\n";
  const auto entries = outlineOf(source);
  MICRONOTES_REQUIRE(entries.size() == 2);
  const auto none = std::numeric_limits<std::size_t>::max();
  // Above the first heading there is no section to be in.
  MICRONOTES_REQUIRE(outlineEntryAt(entries, 0) == none);
  MICRONOTES_REQUIRE(outlineEntryAt(entries, entries[0].offset) == 0);
  MICRONOTES_REQUIRE(outlineEntryAt(entries, source.find("body")) == 0);
  MICRONOTES_REQUIRE(outlineEntryAt(entries, source.find("more")) == 1);
}

MICRONOTES_TEST(outline_of_a_note_with_no_headings_is_empty) {
  MICRONOTES_REQUIRE(outlineOf("").empty());
  MICRONOTES_REQUIRE(outlineOf("just a paragraph\n\nand another\n").empty());
}
