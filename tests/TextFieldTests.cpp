#include "TestSupport.h"

#include "core/editor/TextField.h"

#include <string>

// Key routing for the side inputs. This lives in its own unit so it can be
// tested without an SDL window and, more importantly, so all five fields go
// through one implementation: the version this replaces spelled "Backspace"
// out separately for each field and got it wrong in the same way five times.

namespace {

using microcore::editor::FieldKeyResult;
using microcore::editor::TextField;

}

MICRONOTES_TEST(text_field_backspace_reports_a_change) {
  TextField field;
  field.beginWith("hello", false);
  MICRONOTES_REQUIRE(microcore::editor::applyKeyToField(field, SDLK_BACKSPACE, false, false) == FieldKeyResult::Changed);
  MICRONOTES_REQUIRE(field.text() == "hell");
}

MICRONOTES_TEST(text_field_ctrl_backspace_erases_a_word) {
  TextField field;
  field.beginWith("hello world", false);
  microcore::editor::applyKeyToField(field, SDLK_BACKSPACE, true, false);
  MICRONOTES_REQUIRE(field.text() == "hello ");
}

MICRONOTES_TEST(text_field_backspace_erases_the_selection_not_one_character) {
  TextField field;
  field.beginWith("hello world");  // begins fully selected
  MICRONOTES_REQUIRE(field.editor.hasSelection());
  microcore::editor::applyKeyToField(field, SDLK_BACKSPACE, false, false);
  MICRONOTES_REQUIRE(field.text().empty());
}

MICRONOTES_TEST(text_field_arrows_move_and_report_motion) {
  TextField field;
  field.beginWith("hello", false);
  MICRONOTES_REQUIRE(microcore::editor::applyKeyToField(field, SDLK_LEFT, false, false) == FieldKeyResult::Moved);
  MICRONOTES_REQUIRE(field.editor.cursor() == 4);
  microcore::editor::applyKeyToField(field, SDLK_HOME, false, false);
  MICRONOTES_REQUIRE(field.editor.cursor() == 0);
  microcore::editor::applyKeyToField(field, SDLK_END, false, false);
  MICRONOTES_REQUIRE(field.editor.cursor() == 5);
}

MICRONOTES_TEST(text_field_shift_arrow_selects) {
  TextField field;
  field.beginWith("hello", false);
  microcore::editor::applyKeyToField(field, SDLK_LEFT, false, true);
  microcore::editor::applyKeyToField(field, SDLK_LEFT, false, true);
  MICRONOTES_REQUIRE(field.editor.selectedText() == "lo");
}

MICRONOTES_TEST(text_field_ctrl_arrow_moves_by_word) {
  TextField field;
  field.beginWith("hello world", false);
  microcore::editor::applyKeyToField(field, SDLK_LEFT, true, false);
  MICRONOTES_REQUIRE(field.editor.cursor() == 6);
}

MICRONOTES_TEST(text_field_ignores_keys_it_does_not_own) {
  TextField field;
  field.beginWith("hello", false);
  MICRONOTES_REQUIRE(microcore::editor::applyKeyToField(field, SDLK_RETURN, false, false) == FieldKeyResult::Ignored);
  MICRONOTES_REQUIRE(microcore::editor::applyKeyToField(field, SDLK_A, false, false) == FieldKeyResult::Ignored);
  MICRONOTES_REQUIRE(field.text() == "hello");
}

MICRONOTES_TEST(text_field_begin_with_selects_all_so_typing_replaces) {
  // Opening a rename box on an existing title should let the first keystroke
  // replace it, without losing the ability to edit what was there.
  TextField field;
  field.beginWith("Old title");
  MICRONOTES_REQUIRE(field.editor.selectedText() == "Old title");
  field.editor.insert("N");
  MICRONOTES_REQUIRE(field.text() == "N");
}

MICRONOTES_TEST(text_field_reset_clears_text_and_scroll) {
  TextField field;
  field.beginWith("something long enough to scroll", false);
  field.scrollX = 120.0f;
  field.reset();
  MICRONOTES_REQUIRE(field.empty());
  MICRONOTES_REQUIRE(field.scrollX == 0.0f);
}
