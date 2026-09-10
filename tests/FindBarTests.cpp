#include "TestSupport.h"
#include "ShellFixture.h"

#include "app/FindBar.h"
#include "app/Dismiss.h"
#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/Shell.h"
#include "app/SidebarModel.h"
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
using micronotes::app::activateSidebarRow;
using micronotes::app::openFindFromSearch;
using micronotes::app::openFindInNote;
using micronotes::app::pressSidebarRow;
using micronotes::app::refreshFindMatches;
using micronotes::app::toggleFindOption;

namespace {

// "plan" at 0, "Plan" at 9, "planner" at 14 (so a whole-word search keeps two
// of the three), and "PLAN" at 22.
constexpr const char* kBody = "plan and Plan planner PLAN";


std::size_t activeStart(const UiRuntime& ui) {
  MICRONOTES_REQUIRE(ui.find.hasMatches());
  return ui.find.matches[ui.find.active].start;
}

}

MICRONOTES_TEST(find_bar_finds_every_match_and_starts_on_the_first) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-basic", kBody);

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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-step", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-caret", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-toggles", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-empty", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-edit", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-seed", kBody);
  ui.editor.selectRange(14, 21);  // "planner"
  openFindInNote(ui);
  MICRONOTES_REQUIRE(ui.fields.find.text() == "planner");
  MICRONOTES_REQUIRE(ui.find.matches.size() == 1);

  closeFindInNote(ui);
  UiRuntime multi;
  const micronotes::tests::ScratchNote scratch_multi(multi, "micronotes-find-seed2", "first line\nsecond line\n");
  multi.editor.selectRange(0, 17);
  openFindInNote(multi);
  MICRONOTES_REQUIRE(multi.fields.find.text().empty());
}

MICRONOTES_TEST(find_bar_closing_puts_the_search_away) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-close", kBody);
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
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-escape", kBody);
  ui.fields.find.beginWith("plan");
  openFindInNote(ui);
  ui.focus = micronotes::app::FocusArea::Editor;

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == micronotes::app::Dismissed::Find);
  MICRONOTES_REQUIRE(!ui.find.open);
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) != micronotes::app::Dismissed::Find);
}

// --- carrying a library search into the note ------------------------------
//
// Reaching a note through "Search all notes" and then having to retype the
// query to walk its matches is the step ../microide removed with
// `OpenBufferSearchFromProjectSearchResult`. These are that port.

MICRONOTES_TEST(find_bar_opens_from_a_search_result_carrying_the_query) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-from-search", kBody);
  ui.fields.search.beginWith("plan");
  ui.focus = micronotes::app::FocusArea::Search;

  MICRONOTES_REQUIRE(openFindFromSearch(ui));
  MICRONOTES_REQUIRE(ui.find.open);
  // The query, not a fresh empty field: that is the whole point of the port.
  MICRONOTES_REQUIRE(ui.fields.find.text() == "plan");
  MICRONOTES_REQUIRE(ui.find.matches.size() == 4);
  MICRONOTES_REQUIRE(activeStart(ui) == 0);
  // The keyboard follows the query in, so Enter steps to the next match rather
  // than doing whatever it would have done in the sidebar.
  MICRONOTES_REQUIRE(ui.focus == micronotes::app::FocusArea::Find);
  // Revealed, so the first hit is selected on the page and not merely counted.
  MICRONOTES_REQUIRE(ui.editor.hasSelection());
  MICRONOTES_REQUIRE(ui.editor.selectionStart() == 0);
}

// A library search matches titles too, so a result can be a note whose body
// never mentions the query -- something a grep over files cannot produce, and
// so a case ../microide never had to answer. A focused bar reading "No results"
// over a note the reader asked to read would swallow the next thing they type.
MICRONOTES_TEST(find_bar_declines_a_search_result_whose_body_lacks_the_query) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-title-only", kBody);
  ui.fields.search.beginWith("roadmap");
  ui.focus = micronotes::app::FocusArea::Folders;

  MICRONOTES_REQUIRE(!openFindFromSearch(ui));
  MICRONOTES_REQUIRE(!ui.find.open);
  MICRONOTES_REQUIRE(ui.fields.find.empty());
  // Nothing moved, so the caller's own answer about where the keyboard goes
  // still stands.
  MICRONOTES_REQUIRE(ui.focus == micronotes::app::FocusArea::Folders);
}

// An empty search box cannot seed anything, which is the state the bar is in
// whenever a result row is reached from somewhere other than a live query.
MICRONOTES_TEST(find_bar_declines_a_search_result_with_no_query) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-no-query", kBody);
  MICRONOTES_REQUIRE(!openFindFromSearch(ui));
  MICRONOTES_REQUIRE(!ui.find.open);
}

// Asked for, not passed over. Arrowing down the result list previews each note,
// and moving the keyboard into the find bar on the way would take the arrows
// doing the walking -- the same distinction `RowActivation` already draws for
// tabs and for unfolding notebooks.
MICRONOTES_TEST(a_search_result_carries_its_query_only_when_it_is_asked_for) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-find-activation", kBody);
  // Through the disk, because activating a row reloads the note from it.
  ui.editor.markDirty();
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui, true));
  ui.fields.search.beginWith("plan");

  micronotes::app::SidebarRow result;
  result.kind = micronotes::app::SidebarRow::Kind::SearchResult;
  result.noteId = ui.state.selection().noteId;
  MICRONOTES_REQUIRE(!result.noteId.empty());

  // Arrowed onto: the note is previewed and the sidebar keeps the keyboard.
  activateSidebarRow(ui, result, micronotes::app::RowActivation::Cursor);
  MICRONOTES_REQUIRE(!ui.find.open);

  // Right-clicked: the row is activated the same way -- so the menu that
  // follows acts on this note -- but asking what a result is is not asking to
  // read it, and a find bar must not appear under the menu.
  pressSidebarRow(ui, result, 5.0f, 5.0f, SDL_BUTTON_RIGHT);
  MICRONOTES_REQUIRE(!ui.find.open);
  ui.overlays.close();  // the menu the right click opened

  // Clicked: the query comes with.
  pressSidebarRow(ui, result, 5.0f, 5.0f, SDL_BUTTON_LEFT);
  MICRONOTES_REQUIRE(ui.find.open);
  MICRONOTES_REQUIRE(ui.fields.find.text() == "plan");
  MICRONOTES_REQUIRE(ui.find.matches.size() == 4);
  MICRONOTES_REQUIRE(ui.focus == micronotes::app::FocusArea::Find);
}
