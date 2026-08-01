#include "TestSupport.h"

#include "core/editor/SingleLineEditor.h"
#include "core/editor/SingleLineView.h"
#include "core/util/Utf8.h"

#include <string>
#include <string_view>

// The behaviour a text field is expected to have and the draft strings these
// replaced did not. Each test below corresponds to something that was visibly
// broken when every input was a std::string with `+=` and `pop_back()`.

namespace {

using microcore::editor::SingleLineEditor;

// Eight pixels per code point. Enough to exercise the layout and hit-test
// arithmetic without a font, and it makes the expected numbers obvious.
int fakeWidth(std::string_view run) {
  return static_cast<int>(microcore::util::countCodePoints(run) * 8);
}

}

MICRONOTES_TEST(single_line_backspace_erases_a_whole_code_point) {
  // "café" -- the accented e is two bytes. `pop_back()` erased one of them and
  // left an invalid sequence behind, which is what put replacement glyphs in
  // the search box.
  SingleLineEditor field(std::string("caf\xc3\xa9"));
  MICRONOTES_REQUIRE(field.text() == "caf\xc3\xa9");
  field.erasePrevious();
  MICRONOTES_REQUIRE(field.text() == "caf");
}

MICRONOTES_TEST(single_line_caret_starts_at_the_end_and_moves_by_code_point) {
  SingleLineEditor field(std::string("caf\xc3\xa9"));
  MICRONOTES_REQUIRE(field.cursor() == 5);
  field.moveLeft();
  MICRONOTES_REQUIRE(field.cursor() == 3);  // skipped the whole two-byte é
  field.moveLeft();
  MICRONOTES_REQUIRE(field.cursor() == 2);
  field.moveRight();
  MICRONOTES_REQUIRE(field.cursor() == 3);
}

MICRONOTES_TEST(single_line_inserts_at_the_caret_not_at_the_end) {
  // The old fields appended unconditionally, so fixing a typo in the middle
  // meant deleting everything after it.
  SingleLineEditor field(std::string("note"));
  field.moveHome();
  field.insert("my ");
  MICRONOTES_REQUIRE(field.text() == "my note");
  MICRONOTES_REQUIRE(field.cursor() == 3);
}

MICRONOTES_TEST(single_line_home_and_end_reach_both_edges) {
  SingleLineEditor field(std::string("hello"));
  field.moveHome();
  MICRONOTES_REQUIRE(field.cursor() == 0);
  field.moveEnd();
  MICRONOTES_REQUIRE(field.cursor() == 5);
}

MICRONOTES_TEST(single_line_shift_motion_selects_a_range_not_just_everything) {
  // `bool inputAllSelected` could only ever express "the whole field".
  SingleLineEditor field(std::string("hello world"));
  field.moveWordLeft(true);
  MICRONOTES_REQUIRE(field.hasSelection());
  MICRONOTES_REQUIRE(field.selectedText() == "world");
  MICRONOTES_REQUIRE(field.selectionStart() == 6);
  MICRONOTES_REQUIRE(field.selectionEnd() == 11);
}

MICRONOTES_TEST(single_line_typing_replaces_the_selection) {
  SingleLineEditor field(std::string("hello world"));
  field.moveWordLeft(true);
  field.insert("there");
  MICRONOTES_REQUIRE(field.text() == "hello there");
  MICRONOTES_REQUIRE(!field.hasSelection());
}

MICRONOTES_TEST(single_line_word_erase_removes_a_word) {
  SingleLineEditor field(std::string("hello world"));
  field.eraseWordBefore();
  MICRONOTES_REQUIRE(field.text() == "hello ");
}

MICRONOTES_TEST(single_line_undo_restores_the_previous_value) {
  SingleLineEditor field(std::string("draft"));
  field.insert("!");
  MICRONOTES_REQUIRE(field.text() == "draft!");
  MICRONOTES_REQUIRE(field.undo());
  MICRONOTES_REQUIRE(field.text() == "draft");
  MICRONOTES_REQUIRE(field.redo());
  MICRONOTES_REQUIRE(field.text() == "draft!");
}

MICRONOTES_TEST(single_line_flattens_pasted_newlines) {
  // A pasted paragraph must not smuggle a line break into a note title or a
  // file name, and the field has nowhere to draw one.
  SingleLineEditor field;
  field.insert("one\ntwo\r\nthree");
  MICRONOTES_REQUIRE(field.text() == "one two  three");
  MICRONOTES_REQUIRE(field.text().find('\n') == std::string::npos);
}

MICRONOTES_TEST(single_line_select_all_then_type_clears_the_field) {
  SingleLineEditor field(std::string("old value"));
  field.selectAll();
  MICRONOTES_REQUIRE(field.hasSelection());
  field.insert("n");
  MICRONOTES_REQUIRE(field.text() == "n");
}

// --- view metrics -----------------------------------------------------------

MICRONOTES_TEST(single_line_view_reports_caret_and_selection_positions) {
  SingleLineEditor field(std::string("hello world"));
  field.moveWordLeft(true);
  const auto view = microcore::editor::layoutSingleLine(field, 400.0f, 0.0f, fakeWidth);
  MICRONOTES_REQUIRE(view.hasSelection);
  MICRONOTES_REQUIRE(view.selectionStartX == 48.0f);   // 6 code points
  MICRONOTES_REQUIRE(view.selectionEndX == 88.0f);     // 11 code points
  MICRONOTES_REQUIRE(view.caretX == 48.0f);            // caret at the near edge
  MICRONOTES_REQUIRE(view.scrollX == 0.0f);            // it all fits
}

MICRONOTES_TEST(single_line_view_scrolls_to_keep_the_caret_visible) {
  // Text longer than the box used to simply vanish past the right edge with no
  // way to reach it.
  SingleLineEditor field(std::string("0123456789abcdefghij"));  // 160px of text
  const auto view = microcore::editor::layoutSingleLine(field, 80.0f, 0.0f, fakeWidth);
  MICRONOTES_REQUIRE(view.caretX == 160.0f);
  MICRONOTES_REQUIRE(view.scrollX > 0.0f);
  MICRONOTES_REQUIRE(view.caretX - view.scrollX <= 80.0f);
}

MICRONOTES_TEST(single_line_view_returns_flush_left_once_the_text_fits) {
  SingleLineEditor field(std::string("short"));
  const auto view = microcore::editor::layoutSingleLine(field, 400.0f, 120.0f, fakeWidth);
  MICRONOTES_REQUIRE(view.scrollX == 0.0f);
}

MICRONOTES_TEST(single_line_view_hit_test_lands_on_code_point_boundaries) {
  const std::string text = "caf\xc3\xa9 bar";
  // Just past the middle of the third glyph rounds forward to its far edge.
  MICRONOTES_REQUIRE(microcore::editor::offsetAtX(text, 21.0f, fakeWidth) == 3);
  MICRONOTES_REQUIRE(microcore::editor::offsetAtX(text, 0.0f, fakeWidth) == 0);
  MICRONOTES_REQUIRE(microcore::editor::offsetAtX(text, 1000.0f, fakeWidth) == text.size());
  // The é occupies pixels 24..32 and two bytes; a click inside it must resolve
  // to one of its edges, never to the byte between them.
  const auto inside = microcore::editor::offsetAtX(text, 27.0f, fakeWidth);
  MICRONOTES_REQUIRE(inside == 3 || inside == 5);
}
