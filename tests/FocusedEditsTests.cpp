#include "TestSupport.h"

#include "app/Commands.h"
#include "app/EditCommands.h"
#include "app/Fields.h"
#include "app/FocusedEdits.h"
#include "app/EditCommands.h"
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

// Copy and cut over a selection in the note.
//
// These two are a pair, and they had drifted apart: cut used to fall through
// every branch and do nothing on a selection copy handled. The clipboard cannot
// be read in a test, so these assert on the buffer and the status line, which
// is what the reader sees.
namespace {

// A shell with a note and its first block selected.
void openTwoBlocks(UiRuntime& ui) {
  ui.editor.setText("First block.\n\nSecond block.\n");
  ui.focus = FocusArea::Editor;
  ui.editor.selectRange(0, std::string_view("First block.").size());
}

}

MICRONOTES_TEST(focused_edits_copying_a_selection_leaves_the_note_alone) {
  UiRuntime ui;
  openTwoBlocks(ui);
  const std::string before = ui.editor.text();
  copySelectionInFocus(ui);
  MICRONOTES_REQUIRE(ui.editor.text() == before);
  // Either it reached the clipboard or it said why; what it must not do is
  // nothing at all.
  MICRONOTES_REQUIRE(!ui.status.text.empty());
}

// Cut's contract, which holds whether or not there is a clipboard to reach.
//
// There usually is not, here: these run headless, so `setClipboardText` fails
// and cut takes its refusal path. That makes this the *only* way to state the
// invariant that matters, and it is the one that was broken -- both arms used
// to copy, erase unconditionally, and then report that the copy had failed, so
// a compositor that refused the selection left the text gone from the note and
// absent from the clipboard.
//
// Either the text went somewhere and left the note, or it did neither. Never
// the note without the clipboard.
MICRONOTES_TEST(focused_edits_cut_never_erases_what_it_could_not_copy) {
  UiRuntime ui;
  openTwoBlocks(ui);
  const std::string before = ui.editor.text();

  cutSelectionInFocus(ui);

  const bool erased = ui.editor.text() != before;
  const bool refused = ui.status.text.rfind("Cut failed", 0) == 0;
  micronotes::tests::require(erased != refused,
                             "cut must either remove the selection and say so, or remove nothing "
                             "and say why -- it reported \"" + ui.status.text + "\" and " +
                               (erased ? "erased anyway" : "left the note alone"));
  if(refused) {
    micronotes::tests::require(ui.editor.text() == before,
                               "cut reported a failure and erased the selection anyway");
  }
}

// The same contract for a text selection rather than a block one.
MICRONOTES_TEST(focused_edits_cut_of_a_text_selection_is_all_or_nothing) {
  UiRuntime ui;
  ui.editor.setText("alpha beta\n");
  ui.focus = FocusArea::Editor;
  ui.editor.selectRange(0, 5);
  const std::string before = ui.editor.text();

  cutSelectionInFocus(ui);

  const bool erased = ui.editor.text() != before;
  const bool refused = ui.status.text.rfind("Cut failed", 0) == 0;
  MICRONOTES_REQUIRE(erased != refused);
}

// And copy never changes the buffer whatever the clipboard does, which is the
// property cut now matches rather than the one it used to contradict.
MICRONOTES_TEST(focused_edits_copy_never_changes_the_note) {
  UiRuntime ui;
  openTwoBlocks(ui);
  const std::string before = ui.editor.text();
  copySelectionInFocus(ui);
  MICRONOTES_REQUIRE(ui.editor.text() == before);
  MICRONOTES_REQUIRE(!ui.status.text.empty());
}

// --- block commands over a plain text selection -----------------------------

// A block command applies to every block the selection covers. It used to
// apply to the one block the caret happened to be in: with three list items
// selected by dragging or by Shift+Down, Alt+Up moved one of them and left the
// rest of the selection where it was.
//
// The bodies below are lists, because a block is not a line: "one\ntwo" is one
// paragraph and moving it moves both lines together, which is correct and not
// what anybody was complaining about.

namespace {

// `UiRuntime` holds a database handle and a directory watcher, so it neither
// copies nor moves: it is filled in place.
void putInEditor(UiRuntime& ui, std::string_view body) {
  ui.focus = FocusArea::Editor;
  ui.editor.setText(std::string(body));
}

}

MICRONOTES_TEST(block_commands_read_a_text_selection_as_the_blocks_it_covers) {
  UiRuntime ui;
  putInEditor(ui, "- one\n- two\n- three\n- four\n");
  // "- two\n- three" -- from the start of the second item to the end of the
  // third, which is the shape a drag across two items leaves.
  ui.editor.selectRange(6, 20);
  const auto [from, to] = micronotes::app::blockCommandRange(ui);
  MICRONOTES_REQUIRE(from == 6);
  // One byte inside the last block, not the boundary after it.
  MICRONOTES_REQUIRE(to == 19);
}

MICRONOTES_TEST(block_commands_move_every_selected_line_not_just_the_first) {
  UiRuntime ui;
  putInEditor(ui, "- one\n- two\n- three\n- four\n");
  ui.editor.selectRange(6, 20);  // "- two\n- three"
  MICRONOTES_REQUIRE(micronotes::app::moveSelectedBlocks(ui, -1));
  MICRONOTES_REQUIRE(ui.editor.text() == "- two\n- three\n- one\n- four\n");
  // And back down again, from the selection the move left behind, so a run of
  // Alt+Down carries the same items rather than shedding them one per press.
  MICRONOTES_REQUIRE(micronotes::app::moveSelectedBlocks(ui, 1));
  MICRONOTES_REQUIRE(ui.editor.text() == "- one\n- two\n- three\n- four\n");
}

// The end of a Shift+Down selection sits on the *next* block's first byte, so a
// range taken at face value would carry that block along for the ride.
MICRONOTES_TEST(block_commands_do_not_reach_past_a_selection_that_ends_on_a_boundary) {
  UiRuntime ui;
  putInEditor(ui, "- one\n- two\n- three\n");
  // "- one\n" -- everything up to the start of "- two".
  ui.editor.selectRange(0, 6);
  MICRONOTES_REQUIRE(micronotes::app::moveSelectedBlocks(ui, 1));
  MICRONOTES_REQUIRE(ui.editor.text() == "- two\n- one\n- three\n");
}

MICRONOTES_TEST(block_commands_duplicate_and_delete_the_whole_text_selection) {
  UiRuntime ui;
  putInEditor(ui, "- one\n- two\n- three\n");
  ui.editor.selectRange(0, 12);  // "- one\n- two\n"
  micronotes::app::performBlockCommand(ui, "duplicate");
  MICRONOTES_REQUIRE(ui.editor.text() == "- one\n- two\n- one\n- two\n- three\n");

  UiRuntime other;
  putInEditor(other, "- one\n- two\n- three\n");
  other.editor.selectRange(0, 12);
  micronotes::app::performBlockCommand(other, "delete");
  MICRONOTES_REQUIRE(other.editor.text() == "- three\n");
}

// With no selection at all it is still the block holding the caret, which is
// what a bare Alt+Up has always meant.
MICRONOTES_TEST(block_commands_with_no_selection_are_about_the_block_at_the_caret) {
  UiRuntime ui;
  putInEditor(ui, "- one\n- two\n- three\n");
  ui.editor.moveCursor(8);
  MICRONOTES_REQUIRE(micronotes::app::moveSelectedBlocks(ui, -1));
  MICRONOTES_REQUIRE(ui.editor.text() == "- two\n- one\n- three\n");
}
