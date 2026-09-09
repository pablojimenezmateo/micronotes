#include "TestSupport.h"

#include "ui/NoteProperties.h"

#include <string>
#include <vector>

using micronotes::library::NoteMetadata;
using micronotes::ui::notePropertiesOf;

// A note with nothing in its front matter has nothing to show, and the page
// reserves no room for a header that would be empty.
MICRONOTES_TEST(note_properties_are_empty_when_the_front_matter_is) {
  MICRONOTES_REQUIRE(notePropertiesOf(NoteMetadata {}).empty());
}

// The keys micronotes writes for itself are the note's identity, not facts
// about it: the page shows the title as the title and never as a row.
MICRONOTES_TEST(note_properties_leave_out_the_notes_own_identity) {
  NoteMetadata metadata;
  metadata.id = "n123";
  metadata.title = "Roadmap";
  metadata.icon = "\xf0\x9f\x93\x8c";
  metadata.tags = {"planning"};
  MICRONOTES_REQUIRE(notePropertiesOf(metadata).empty());
}

// Tags are shown, but not here. They were a row of chips above the note's first
// line -- a band of ground and a coloured dot per tag, ahead of the note's own
// text -- and they are eight pixels on the breadcrumb one line higher now.
MICRONOTES_TEST(note_properties_leave_the_tags_to_the_breadcrumb) {
  NoteMetadata metadata;
  metadata.tags = {"planning", "work"};
  metadata.extra = {"aliases: plan"};
  const auto rows = notePropertiesOf(metadata);
  MICRONOTES_REQUIRE(rows.size() == 1);
  MICRONOTES_REQUIRE(rows[0].key == "aliases");
  MICRONOTES_REQUIRE(rows[0].value == "plan");
}

// The parser hands over the source lines of a key it does not model. A scalar
// is one line, and its value is whatever followed the colon.
MICRONOTES_TEST(note_properties_read_a_scalar_key_from_its_line) {
  NoteMetadata metadata;
  metadata.extra = {"created:   2026-09-02  "};
  const auto rows = notePropertiesOf(metadata);
  MICRONOTES_REQUIRE(rows.size() == 1);
  MICRONOTES_REQUIRE(rows[0].key == "created");
  MICRONOTES_REQUIRE(rows[0].value == "2026-09-02");
}

// A value written as a YAML block sequence arrives as the key's line plus one
// line per item. All of it is one row, flattened, or an eleven-item list would
// push the note itself off the bottom of the page.
MICRONOTES_TEST(note_properties_gather_a_block_sequence_into_one_row) {
  NoteMetadata metadata;
  metadata.extra = {"aliases:", "  - Plan", "  - Q3 Plan"};
  const auto rows = notePropertiesOf(metadata);
  MICRONOTES_REQUIRE(rows.size() == 1);
  MICRONOTES_REQUIRE(rows[0].key == "aliases");
  MICRONOTES_REQUIRE(rows[0].value == "Plan, Q3 Plan");
}

// An indented continuation of a scalar joins the value it continues rather than
// starting a row of its own.
MICRONOTES_TEST(note_properties_join_an_indented_continuation_to_its_key) {
  NoteMetadata metadata;
  metadata.extra = {"summary:", "  the first half", "  the second half", "status: draft"};
  const auto rows = notePropertiesOf(metadata);
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(rows[0].value == "the first half, the second half");
  MICRONOTES_REQUIRE(rows[1].key == "status");
  MICRONOTES_REQUIRE(rows[1].value == "draft");
}

// A header that opens with a stray continuation is malformed, not a row: there
// is no key for it to belong to, and inventing one would name it wrongly.
MICRONOTES_TEST(note_properties_drop_a_continuation_with_no_key_above_it) {
  NoteMetadata metadata;
  metadata.extra = {"  - orphaned", "real: value"};
  const auto rows = notePropertiesOf(metadata);
  MICRONOTES_REQUIRE(rows.size() == 1);
  MICRONOTES_REQUIRE(rows[0].key == "real");
}
