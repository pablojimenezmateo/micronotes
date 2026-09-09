#include "TestSupport.h"

#include "app/Commands.h"
#include "app/EditCommands.h"
#include "app/Fields.h"
#include "app/FocusedEdits.h"
#include "app/Shell.h"
#include "app/SettingsPane.h"
#include "ui/Actions.h"

#include <string>
#include <string_view>

using micronotes::app::FocusArea;
using micronotes::app::UiRuntime;
using micronotes::app::focusedField;
using micronotes::app::performCommand;
using micronotes::app::redoInFocus;
using micronotes::app::selectAllInFocus;
using micronotes::app::undoInFocus;

// The six editing verbs that answer to the focus.
//
// The case worth pinning is the one that was broken: `Undo` and `Redo` are Edit
// menu items whose `keyRunsIt` is false, so the menu ran `performCommand`'s
// branch and the key ran `handleKey`'s. The two disagreed -- `performCommand`
// handled the note buffer and not a focused one-line field -- so with the caret
// in a rename box `Ctrl+Z` undid and choosing Undo from the menu did nothing.
//
// Both paths are now one function, and these tests drive the dispatch rather
// than the function directly: calling `undoInFocus` twice would pass whatever
// `performCommand` did.

namespace {

// A field with something to undo: type into it through its own editor so the
// field's undo stack has a step on it.
void typeIntoRenameField(UiRuntime& ui, const std::string& text) {
  ui.focus = FocusArea::RenameNote;
  auto* field = focusedField(ui);
  micronotes::tests::require(field != nullptr, "RenameNote is expected to be a field");
  field->editor.setText("");
  for(const char c : text) field->editor.insert(std::string_view(&c, 1));
}

}

MICRONOTES_TEST(focused_edits_undo_from_the_menu_reaches_a_focused_field) {
  UiRuntime ui;
  typeIntoRenameField(ui, "abc");
  const std::string typed = focusedField(ui)->editor.text();
  MICRONOTES_REQUIRE(typed == "abc");

  // The Edit menu's route. Before these verbs were one function this did
  // nothing at all, because it only looked at the note buffer.
  performCommand(ui, "undo");
  MICRONOTES_REQUIRE(focusedField(ui)->editor.text() != typed);
}

MICRONOTES_TEST(focused_edits_redo_from_the_menu_reaches_a_focused_field) {
  UiRuntime ui;
  typeIntoRenameField(ui, "abc");
  const std::string typed = focusedField(ui)->editor.text();

  performCommand(ui, "undo");
  const std::string undone = focusedField(ui)->editor.text();
  MICRONOTES_REQUIRE(undone != typed);

  performCommand(ui, "redo");
  MICRONOTES_REQUIRE(focusedField(ui)->editor.text() == typed);
}

// The verb and the command are the same code, which is the whole point: the
// two routes cannot answer differently because there is only one answer.
MICRONOTES_TEST(focused_edits_the_key_and_the_menu_take_the_same_route) {
  UiRuntime viaCommand;
  typeIntoRenameField(viaCommand, "hello");
  performCommand(viaCommand, "undo");

  UiRuntime viaKey;
  typeIntoRenameField(viaKey, "hello");
  undoInFocus(viaKey);

  MICRONOTES_REQUIRE(focusedField(viaCommand)->editor.text() ==
                     focusedField(viaKey)->editor.text());
}

// With no field focused the verbs fall through to the note buffer, and with
// neither they do nothing rather than reaching for a null field.
MICRONOTES_TEST(focused_edits_do_nothing_when_nothing_can_take_them) {
  UiRuntime ui;
  ui.focus = FocusArea::Folders;
  MICRONOTES_REQUIRE(focusedField(ui) == nullptr);
  // Each of these used to be a branch in the router that simply had no arm for
  // this focus; they must stay silent rather than crash.
  undoInFocus(ui);
  redoInFocus(ui);
  selectAllInFocus(ui);
  performCommand(ui, "undo");
  performCommand(ui, "redo");
  MICRONOTES_REQUIRE(!ui.editor.hasSelection());
}

MICRONOTES_TEST(focused_edits_select_all_selects_the_focused_field) {
  UiRuntime ui;
  typeIntoRenameField(ui, "abc");
  MICRONOTES_REQUIRE(!focusedField(ui)->editor.hasSelection());
  selectAllInFocus(ui);
  MICRONOTES_REQUIRE(focusedField(ui)->editor.hasSelection());
  MICRONOTES_REQUIRE(focusedField(ui)->editor.selectedText() == "abc");
}

// The keyboard's block-shape chords and the table that produces them.
//
// The router used to name the kind, the level and the label for each digit a
// second time, and the labels had drifted -- "heading 1" from the keyboard
// against "Heading 1" from the menu, for the same shape in the same note. The
// digit lives in `blockKinds()` now, so there is one answer; this checks the
// two things a single table can still get wrong.
MICRONOTES_TEST(block_kind_chords_are_unique_and_resolve_to_a_shape) {
  int bound = 0;
  for(const auto& entry : micronotes::app::blockKinds()) {
    if(entry.chordDigit == 0) continue;
    ++bound;
    // A digit is claimed by at most one shape, or the second one is dead.
    const auto* found = micronotes::app::blockKindForChordDigit(entry.chordDigit);
    micronotes::tests::require(found == &entry,
                               std::string("two block kinds claim Ctrl+Shift+") + entry.chordDigit);
  }
  MICRONOTES_REQUIRE(bound > 0);
}

// And what the shortcut list shows is exactly what the table binds.
//
// This reads `aboutRows()` -- the real F1 list -- rather than a hint string,
// because the artefact the reader sees is what was wrong. It used to carry
// three rows for this one family, from two hand-written tables, and no two of
// them agreed: `turn-into`'s hint claimed "Ctrl+Shift+1-9" while 4, 5 and 6
// were bound to nothing, and two `helpRows` entries spelled the same digits
// two more ways.
MICRONOTES_TEST(block_kind_chords_appear_in_the_shortcut_list_exactly_when_they_work) {
  const auto rows = micronotes::app::aboutRows();
  std::string listed;
  for(const auto& row : rows) listed += row.label + " | " + row.detail + "\n";

  for(char digit = '0'; digit <= '9'; ++digit) {
    micronotes::ui::KeyChord chord;
    chord.ctrl = true;
    chord.shift = true;
    chord.key = static_cast<SDL_Keycode>(digit);
    const std::string keys = micronotes::ui::formatKeyChord(chord);
    const bool shown = listed.find(" | " + keys + "\n") != std::string::npos;
    const bool works = micronotes::app::blockKindForChordDigit(digit) != nullptr;
    micronotes::tests::require(
      shown == works,
      keys + (shown ? " is in the shortcut list but no block kind claims it"
                    : " turns a block into something but the shortcut list does not show it") +
        "\n--- list ---\n" + listed);
  }
}

// One row per shape, naming the shape -- the point of deriving it. A reader
// asking "how do I make a heading 2" gets an answer instead of a digit range.
MICRONOTES_TEST(block_kind_shortcut_rows_name_the_shape_they_make) {
  const auto rows = micronotes::app::aboutRows();
  for(const auto& entry : micronotes::app::blockKinds()) {
    if(entry.chordDigit == 0) continue;
    bool found = false;
    for(const auto& row : rows) {
      if(row.label.find(entry.label) != std::string::npos) found = true;
    }
    micronotes::tests::require(found, std::string("no shortcut row names the shape \"") +
                                        entry.label + "\" that a digit is bound to");
  }
}
