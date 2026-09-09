#include "TestSupport.h"

#include "app/Commands.h"
#include "app/Fields.h"
#include "app/FocusedEdits.h"
#include "app/Shell.h"

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
