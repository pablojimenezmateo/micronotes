#include "TestSupport.h"

#include "ui/Outline.h"

#include "doc/Layout.h"

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

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

// TD-50's proof, and it is the same proof the twelfth, thirteenth and sixteenth
// passes used: an incremental anything is only worth having if it is *exactly*
// the full computation. So the assertion is equality with `outlineInto` over a
// cold partition, after every one of a long random sequence of splices — not a
// list of cases somebody thought of, because the cases that break a splice are
// the ones nobody thinks of.
//
// The alphabet is chosen to make headings dense and to keep edits landing on
// and around them: `#`, a newline, a space and two letters. A note written out
// of those grows and loses headings constantly, changes a heading's level by
// one typed character, turns a heading into a paragraph by deleting its space,
// and merges two of them by deleting a newline — which is the whole family of
// things the block rescan has to get right for this to hold.
MICRONOTES_TEST(outline_update_equals_a_cold_outline_over_a_long_edit_sequence) {
  micronotes::doc::DocumentLayout layout;
  micronotes::doc::Metrics metrics;
  // A fixed-advance stand-in: the outline does not care what anything measures,
  // and a real face would make this a font test.
  metrics.measure = [](std::string_view text, const micronotes::doc::RunStyle&) {
    return static_cast<float>(text.size()) * 7.0f;
  };
  metrics.lineHeight = [](const micronotes::doc::RunStyle&) { return 18.0f; };
  layout.setMetrics(metrics);

  std::string source = "# One\n\nbody\n\n## Two\n\ntail\n";
  std::uint64_t revision = 1;
  const auto relayout = [&] {
    micronotes::doc::LayoutOptions options;
    options.width = 600.0f;
    options.sourceRevision = revision;
    layout.update(source, options);
  };
  relayout();

  std::vector<micronotes::ui::OutlineEntry> standing;
  micronotes::ui::outlineInto(source, layout.blocksAt(revision), &standing);
  std::uint64_t standingAt = revision;

  std::mt19937 rng(20260913u);
  const std::string alphabet = "#\n ab";
  std::size_t taken = 0;
  constexpr int kEdits = 600;
  for(int step = 0; step < kEdits; ++step) {
    // Insert, or erase when there is enough to erase from. Lengths of one and
    // two, so a splice lands mid-heading as often as between blocks.
    const std::size_t at = std::uniform_int_distribution<std::size_t>(0, source.size())(rng);
    // An erase has to erase something. A zero-length one leaves the bytes
    // identical, which the layout answers by reusing what it has and *not*
    // advancing its stamp -- so `blocksAt` then correctly refuses a revision
    // the layout never saw, and the step would be measuring that instead.
    if(source.size() > 24 && at < source.size() && (rng() & 1u) != 0u) {
      const std::size_t length =
        std::min<std::size_t>(1 + (rng() % 2), source.size() - at);
      source.erase(at, length);
    } else {
      std::string insert;
      const std::size_t length = 1 + (rng() % 2);
      for(std::size_t i = 0; i < length; ++i) insert.push_back(alphabet[rng() % alphabet.size()]);
      source.insert(at, insert);
    }
    ++revision;
    relayout();
    const micronotes::doc::BlockSpan blocks = layout.blocksAt(revision);
    MICRONOTES_REQUIRE(!blocks.empty());

    std::vector<micronotes::ui::OutlineEntry> spliced = standing;
    const bool took = micronotes::ui::outlineUpdate(source, blocks, layout.blockRelay(),
                                                    standingAt, &spliced);
    if(took) ++taken;

    // The answer, whichever way it was reached, has to be the cold one.
    std::vector<micronotes::ui::OutlineEntry> cold;
    micronotes::ui::outlineInto(source, blocks, &cold);
    const std::vector<micronotes::ui::OutlineEntry>& got = took ? spliced : cold;
    MICRONOTES_REQUIRE(got.size() == cold.size());
    for(std::size_t i = 0; i < cold.size(); ++i) {
      MICRONOTES_REQUIRE(got[i].offset == cold[i].offset);
      MICRONOTES_REQUIRE(got[i].level == cold[i].level);
      MICRONOTES_REQUIRE(got[i].depth == cold[i].depth);
      MICRONOTES_REQUIRE(got[i].text == cold[i].text);
    }
    standing = std::move(cold);
    standingAt = revision;
  }

  // And the splice has to actually be *taken*, or every assertion above is a
  // test of `outlineInto` against itself. This is the half that a change which
  // quietly stopped taking the bound would fail.
  MICRONOTES_REQUIRE(taken > static_cast<std::size_t>(kEdits) * 8 / 10);
}

// The refusals, each of which has to change nothing and say so, because a
// caller that is told "taken" about a value that was not updated shows a stale
// outline with nothing failing.
MICRONOTES_TEST(outline_update_refuses_a_relay_it_cannot_stand_on) {
  const std::string source = "# One\n\nbody\n";
  const auto blocks = micronotes::doc::scanBlocks(source);
  std::vector<micronotes::ui::OutlineEntry> entries = outlineOf(source);
  const auto before = entries;

  const auto refused = [&](const micronotes::doc::BlockRelay& relay, std::uint64_t at,
                           micronotes::doc::BlockSpan span) {
    std::vector<micronotes::ui::OutlineEntry> copy = before;
    const bool took = micronotes::ui::outlineUpdate(source, span, relay, at, &copy);
    MICRONOTES_REQUIRE(!took);
    MICRONOTES_REQUIRE(copy.size() == before.size());
    for(std::size_t i = 0; i < before.size(); ++i) {
      MICRONOTES_REQUIRE(copy[i].offset == before[i].offset && copy[i].text == before[i].text);
    }
  };

  const micronotes::doc::BlockSpan span(blocks);
  // Nothing to say: the layout could not describe its last update.
  refused({}, 7, span);
  // A relay from a revision this outline was not built at -- two updates landed
  // between frames, or one was skipped.
  micronotes::doc::BlockRelay relay;
  relay.fromRevision = 7;
  relay.toRevision = 8;
  relay.headBlocks = 1;
  refused(relay, 6, span);
  // No partition to splice against.
  refused(relay, 7, {});
  // A relay whose bands do not fit the partition it was handed, which is what a
  // caller that scanned its own blocks instead of borrowing the layout's looks
  // like.
  micronotes::doc::BlockRelay wide = relay;
  wide.headBlocks = span.size() + 1;
  refused(wide, 7, span);
}
