#include "TestSupport.h"

#include "app/FindBar.h"
#include "app/Dismiss.h"
#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/Shell.h"
#include "ui/TextRenderer.h"

#include <filesystem>
#include <fstream>
#include <string>

// The find bar as a model: what it found, which match it is on, and what a step
// or a toggle does to both. None of this needs a window -- `micronotes_shell`
// is a library the test binary links, so the bar's verbs are ordinary
// functions over a runtime.

using micronotes::app::FindToggle;
using micronotes::app::UiRuntime;
using micronotes::app::closeFindInNote;
using micronotes::app::findBarLayout;
using micronotes::app::findMatchCountText;
using micronotes::app::moveFindMatch;
using micronotes::app::openFindInNote;
using micronotes::app::refreshFindMatches;
using micronotes::app::toggleFindOption;

namespace {

// "plan" at 0, "Plan" at 9, "planner" at 14 (so a whole-word search keeps two
// of the three), and "PLAN" at 22.
constexpr const char* kBody = "plan and Plan planner PLAN";

std::filesystem::path scratchRoot(const char* name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

// A shell with one note open and a known body in the buffer.
void openScratchNote(UiRuntime& ui, const char* name, const std::string& body) {
  const auto root = scratchRoot(name);
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::createNote(ui, "Note");
  ui.editor.setText(body);
}

std::size_t activeStart(const UiRuntime& ui) {
  MICRONOTES_REQUIRE(ui.find.hasMatches());
  return ui.find.matches[ui.find.active].start;
}

}

MICRONOTES_TEST(find_bar_finds_every_match_and_starts_on_the_first) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-basic", kBody);

  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.find.open);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 4);
  MICRONOTES_REQUIRE(activeStart(ui) == 0);
  MICRONOTES_REQUIRE(findMatchCountText(ui) == "1 of 4");
  // Opening reveals, and revealing selects: the match the reader is on is the
  // buffer's selection, which is what marks it on the page and what Esc leaves
  // behind for a copy.
  MICRONOTES_REQUIRE(ui.editor.hasSelection());
  MICRONOTES_REQUIRE(ui.editor.selectionStart() == 0);
  MICRONOTES_REQUIRE(ui.editor.selectionEnd() == 4);
}

MICRONOTES_TEST(find_bar_steps_through_matches_and_wraps_both_ways) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-step", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);

  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(activeStart(ui) == 9);
  MICRONOTES_REQUIRE(findMatchCountText(ui) == "2 of 4");
  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(activeStart(ui) == 14);
  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(activeStart(ui) == 22);
  // Off the end and back to the top.
  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(activeStart(ui) == 0);
  // And off the top to the bottom.
  moveFindMatch(ui, -1);
  MICRONOTES_REQUIRE(activeStart(ui) == 22);
}

// The step is measured from the caret, not from a remembered index, so a click
// in the note between two presses is honoured. The index would have stepped
// from wherever the last press left it, which may be a long way behind.
MICRONOTES_TEST(find_bar_steps_from_the_caret_not_from_the_last_step) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-caret", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(activeStart(ui) == 0);

  // As if the reader had clicked near the end of the note.
  ui.editor.moveCursor(20);
  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(activeStart(ui) == 22);

  ui.editor.moveCursor(20);
  moveFindMatch(ui, -1);
  MICRONOTES_REQUIRE(activeStart(ui) == 14);
}

MICRONOTES_TEST(find_bar_toggles_change_what_counts_as_a_match) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-toggles", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 4);

  toggleFindOption(ui, FindToggle::MatchCase);
  MICRONOTES_REQUIRE(ui.find.options.matchCase);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 2);  // "plan" and "planner"

  toggleFindOption(ui, FindToggle::WholeWord);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 1);  // "planner" is out
  MICRONOTES_REQUIRE(activeStart(ui) == 0);

  toggleFindOption(ui, FindToggle::MatchCase);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 3);  // whole word, any case
}

MICRONOTES_TEST(find_bar_says_when_there_is_nothing_to_find) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-empty", kBody);
  ui.fields.find.beginWith("zebra");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(!ui.find.hasMatches());
  MICRONOTES_REQUIRE(findMatchCountText(ui) == "No results");
  // Stepping with nothing to step to does nothing at all, rather than indexing
  // an empty list.
  moveFindMatch(ui, 1);
  MICRONOTES_REQUIRE(!ui.find.hasMatches());

  // An empty query is not "no results", it is no question: the bar prints
  // nothing rather than telling the reader their empty box failed.
  ui.fields.find.reset();
  refreshFindMatches(ui);
  MICRONOTES_REQUIRE(findMatchCountText(ui).empty());
}

// The match set addresses the buffer, so an edit under an open bar has to move
// it. The memo is keyed on the revision, which is what makes that automatic.
MICRONOTES_TEST(find_bar_follows_the_buffer_when_the_note_is_edited) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-edit", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 4);

  ui.editor.setText(std::string(kBody) + " plan");
  refreshFindMatches(ui);
  MICRONOTES_REQUIRE(ui.find.matches.size() == 5);
  MICRONOTES_REQUIRE(ui.find.matches.back().start == 27);
}

// Opening on a selection is what makes the key worth pressing after
// highlighting a word. A selection spanning a line is not a needle anybody
// meant, so it is left alone.
MICRONOTES_TEST(find_bar_opens_on_the_selection) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-seed", kBody);
  ui.editor.selectRange(14, 21);  // "planner"
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.fields.find.text() == "planner");
  MICRONOTES_REQUIRE(ui.find.matches.size() == 1);

  closeFindInNote(ui);
  UiRuntime multi;
  openScratchNote(multi, "micronotes-find-seed2", "first line\nsecond line\n");
  multi.editor.selectRange(0, 17);
  openFindInNote(multi);
  MICRONOTES_REQUIRE(multi.fields.find.text().empty());
}

MICRONOTES_TEST(find_bar_closing_puts_the_search_away) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-close", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.find.hasMatches());

  closeFindInNote(ui);
  MICRONOTES_REQUIRE(!ui.find.open);
  MICRONOTES_REQUIRE(!ui.find.hasMatches());
  MICRONOTES_REQUIRE(ui.fields.find.empty());
  MICRONOTES_REQUIRE(ui.focus == micronotes::app::FocusArea::Editor);
  // The match walked to is still selected. Closing the bar is putting the
  // search away, not undoing where it took you.
  MICRONOTES_REQUIRE(ui.editor.hasSelection());
}

// One geometry for the paint and the hit test: two copies of a button's rect is
// a button that moves out from under the pointer.
MICRONOTES_TEST(find_bar_layout_keeps_its_controls_inside_the_card) {
  micronotes::ui::TextRenderer text {nullptr};
  const micronotes::ui::Rect content {200.0f, 100.0f, 900.0f, 700.0f};
  MICRONOTES_REQUIRE(micronotes::ui::empty(findBarLayout(content, false, text).bar));

  const auto layout = findBarLayout(content, true, text);
  const auto inside = [&](micronotes::ui::Rect box) {
    return box.x >= layout.bar.x && box.y >= layout.bar.y &&
           box.x + box.w <= layout.bar.x + layout.bar.w &&
           box.y + box.h <= layout.bar.y + layout.bar.h;
  };
  MICRONOTES_REQUIRE(!micronotes::ui::empty(layout.bar));
  MICRONOTES_REQUIRE(inside(layout.field));
  MICRONOTES_REQUIRE(inside(layout.count));
  MICRONOTES_REQUIRE(inside(layout.previous));
  MICRONOTES_REQUIRE(inside(layout.next));
  MICRONOTES_REQUIRE(inside(layout.close));
  for(const auto& toggle : layout.toggles) MICRONOTES_REQUIRE(inside(toggle));

  // Left to right, in the order they are read and pressed, with nothing
  // overlapping anything beside it.
  MICRONOTES_REQUIRE(layout.field.x + layout.field.w <= layout.toggles.front().x);
  MICRONOTES_REQUIRE(layout.toggles.front().x < layout.toggles.back().x);
  MICRONOTES_REQUIRE(layout.toggles.back().x + layout.toggles.back().w <= layout.count.x);
  MICRONOTES_REQUIRE(layout.count.x + layout.count.w <= layout.previous.x);
  MICRONOTES_REQUIRE(layout.previous.x + layout.previous.w <= layout.next.x);
  MICRONOTES_REQUIRE(layout.next.x + layout.next.w <= layout.close.x);

  // Clear of the page's scrollbar lane, whether or not one is showing: a bar
  // that shifts sideways when a note grows long enough to scroll is a bar whose
  // buttons move for a reason the reader cannot see.
  MICRONOTES_REQUIRE(layout.bar.x + layout.bar.w < content.x + content.w);
}

// Escape puts one narrowing away at a time, and the find bar is one of them --
// the *bar*, not the field: a reader who clicked into the note to look at a
// match still has the highlights over the page.
MICRONOTES_TEST(find_bar_is_what_escape_closes) {
  UiRuntime ui;
  openScratchNote(ui, "micronotes-find-escape", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  ui.focus = micronotes::app::FocusArea::Editor;

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == micronotes::app::Dismissed::Find);
  MICRONOTES_REQUIRE(!ui.find.open);
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) != micronotes::app::Dismissed::Find);
}
