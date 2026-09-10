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

// The incremental rewrap, against the only oracle that matters: the full one.
//
// `softWrapUpdate` bounds the work by the edit, which means it is right exactly
// when its output is byte-for-byte the wrap the whole note would have given.
// So the test is not a list of cases it should handle -- it is that equality,
// asserted after every edit of a sequence that covers the shapes with an edge
// in them: an insert at offset zero, one that lands on a line break, a deletion
// that joins two logical lines, an insert that splits one into three, and a
// replacement that spans several.
namespace {

struct WrapCase {
  std::size_t start = 0;
  std::size_t end = 0;
  const char* text = "";
};

bool sameRows(const std::vector<micronotes::editor::SoftWrapRow>& a,
              const std::vector<micronotes::editor::SoftWrapRow>& b) {
  if(a.size() != b.size()) return false;
  for(std::size_t i = 0; i < a.size(); ++i) {
    if(a[i].start != b[i].start || a[i].end != b[i].end) return false;
  }
  return true;
}

}

MICRONOTES_TEST(editor_soft_wrap_update_agrees_with_a_full_rewrap_after_every_edit) {
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  constexpr int kWidth = 11;

  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha beta gamma\ndelta epsilon zeta\n\neta theta\n");

  std::vector<micronotes::editor::SoftWrapRow> rows;
  micronotes::editor::softWrapInto(rows, editor.text(), kWidth, measure);
  micronotes::editor::SoftWrapScratch scratch;

  const WrapCase cases[] = {
    {0, 0, "X"},              // at the very first byte
    {7, 7, " inserted words "},  // inside the first line, forcing new rows
    {4, 12, ""},              // a deletion inside one line
    {16, 16, "\n"},           // a break the writer put in: one line becomes two
    {2, 2, "\n\n\n"},         // three at once, two of them empty lines
    {0, 1, ""},               // remove the first byte again
    {9, 10, ""},              // remove a line break: two lines join
    {5, 30, "one two three four five six"},  // a replacement spanning several
    {0, 0, "\n"},             // an empty line at the top
  };

  for(const WrapCase& change : cases) {
    const std::size_t size = editor.text().size();
    const std::size_t start = std::min(change.start, size);
    const std::size_t end = std::min(std::max(change.end, start), size);
    editor.replaceRange(start, end, change.text);

    micronotes::editor::softWrapUpdate(rows, scratch, editor.text(), editor.lastChange(), kWidth,
                                       measure);
    std::vector<micronotes::editor::SoftWrapRow> expected;
    micronotes::editor::softWrapInto(expected, editor.text(), kWidth, measure);
    MICRONOTES_REQUIRE(sameRows(rows, expected));
  }
}

// The same equality over a long deterministic sequence, because the cases above
// are the ones somebody thought of. A wrap is a partition of the buffer, and
// the failures worth catching are the ones where an edit lands a byte away from
// where the reasoning assumed -- which nobody enumerates correctly.
MICRONOTES_TEST(editor_soft_wrap_update_agrees_with_a_full_rewrap_over_a_long_sequence) {
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  constexpr int kWidth = 9;

  micronotes::editor::MarkdownEditor editor;
  editor.setText("the quick brown fox\njumps over\n\nthe lazy dog\n");

  std::vector<micronotes::editor::SoftWrapRow> rows;
  micronotes::editor::softWrapInto(rows, editor.text(), kWidth, measure);
  micronotes::editor::SoftWrapScratch scratch;

  std::uint64_t seed = 0x9E3779B97F4A7C15ull;
  const auto next = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  static constexpr std::string_view kInserts[] = {"", "a", " ", "\n", "word ", "\n\n", "xy z\nw"};

  for(int step = 0; step < 400; ++step) {
    const std::size_t size = editor.text().size();
    const std::size_t start = size == 0 ? 0 : static_cast<std::size_t>(next() % (size + 1));
    const std::size_t span = static_cast<std::size_t>(next() % 6);
    const std::size_t end = std::min(start + span, size);
    editor.replaceRange(start, end, kInserts[next() % std::size(kInserts)]);

    micronotes::editor::softWrapUpdate(rows, scratch, editor.text(), editor.lastChange(), kWidth,
                                       measure);
    std::vector<micronotes::editor::SoftWrapRow> expected;
    micronotes::editor::softWrapInto(expected, editor.text(), kWidth, measure);
    MICRONOTES_REQUIRE(sameRows(rows, expected));
  }
}

// Rows partition the whole buffer, so their last one ends at its size -- which
// makes a stale set detectable in O(1) whenever the lengths do not line up, and
// `softWrapUpdate` rebuilds rather than splicing into a partition that
// describes some other buffer. Not the whole question (two buffers of a length
// wrap differently, and the caller's revision stamps are what answer that), but
// it is the half that catches a caller which skipped an edit.
MICRONOTES_TEST(editor_soft_wrap_update_rebuilds_when_the_rows_are_not_the_buffers) {
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha beta gamma delta");

  std::vector<micronotes::editor::SoftWrapRow> rows;
  micronotes::editor::softWrapInto(rows, editor.text(), 11, measure);
  micronotes::editor::SoftWrapScratch scratch;

  // Two edits, one update: the rows describe the buffer before both.
  editor.replaceRange(0, 5, "one");
  editor.replaceRange(0, 3, "seventeen letters here");
  micronotes::editor::softWrapUpdate(rows, scratch, editor.text(), editor.lastChange(), 11, measure);

  std::vector<micronotes::editor::SoftWrapRow> expected;
  micronotes::editor::softWrapInto(expected, editor.text(), 11, measure);
  MICRONOTES_REQUIRE(sameRows(rows, expected));
}
