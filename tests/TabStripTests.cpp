#include "TestSupport.h"
#include "TempDir.h"

#include "app/Notes.h"
#include "app/PageView.h"
#include "app/SessionState.h"
#include "app/Shell.h"
#include "app/TabStrip.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The strip as state rather than as pixels: which tabs a bulk close takes, and
// where each tab remembers it was left.
//
// Both are reached from the pointer in the running app, and both were the sort
// of thing that only shows up in use -- a "close others" that walks the strip
// forwards closes the wrong notes from its second step on, and a scroll offset
// that belongs to the window instead of the note is invisible until two notes
// of different lengths are open.

using micronotes::app::TabCloseScope;
using micronotes::app::UiRuntime;
using micronotes::app::closeTabs;

namespace {

// A library of `count` notes, every one of them open in a tab of its own.
class OpenNotes {
public:
  OpenNotes(UiRuntime& ui, std::string_view name, std::size_t count) : dir_(name) {
    std::filesystem::create_directories(dir_.path());
    micronotes::tests::require(micronotes::app::openLibraryRoot(ui, dir_.path()),
                               "the scratch library did not open");
    for(std::size_t i = 0; i < count; ++i) {
      micronotes::app::createNote(ui, "Note " + std::to_string(i));
      micronotes::tests::require(micronotes::app::saveCurrent(ui), "the note did not save");
    }
    micronotes::tests::require(ui.state.workspace().tabs.size() == count,
                               "the notes did not each open a tab");
  }

private:
  micronotes::tests::TempDir dir_;
};

std::string titlesOf(const UiRuntime& ui) {
  std::string out;
  for(const auto& tab : ui.state.workspace().tabs) {
    const auto note = ui.state.catalog().findNote(tab.noteId);
    if(!out.empty()) out += ",";
    out += note ? note->title : "?";
  }
  return out;
}

}

MICRONOTES_TEST(tab_strip_close_others_leaves_the_tab_it_was_asked_about) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-others", 5);
  // The second tab, which is neither the one showing nor an end of the strip:
  // a bulk close that quietly means "the active one" would pass this by
  // accident on any other choice.
  MICRONOTES_REQUIRE(closeTabs(ui, 1, TabCloseScope::Others) == 4);
  MICRONOTES_REQUIRE(titlesOf(ui) == "Note 1");
  MICRONOTES_REQUIRE(ui.state.workspace().activeTab == 0);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == ui.state.workspace().tabs[0].noteId);
}

MICRONOTES_TEST(tab_strip_close_to_the_right_and_left_keep_their_own_side) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-sides", 5);
  MICRONOTES_REQUIRE(closeTabs(ui, 2, TabCloseScope::ToRight) == 2);
  MICRONOTES_REQUIRE(titlesOf(ui) == "Note 0,Note 1,Note 2");
  MICRONOTES_REQUIRE(closeTabs(ui, 2, TabCloseScope::ToLeft) == 2);
  MICRONOTES_REQUIRE(titlesOf(ui) == "Note 2");
}

// The descending walk is what makes this right. Forwards, closing tab 0
// renumbers every tab after it, so the second step closes what was tab 2 and
// the last steps run off the end of a strip that has since shrunk.
MICRONOTES_TEST(tab_strip_close_to_the_right_closes_exactly_the_right_notes) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-right-order", 6);
  MICRONOTES_REQUIRE(closeTabs(ui, 0, TabCloseScope::ToRight) == 5);
  MICRONOTES_REQUIRE(titlesOf(ui) == "Note 0");
}

MICRONOTES_TEST(tab_strip_close_all_empties_the_strip) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-all", 4);
  MICRONOTES_REQUIRE(closeTabs(ui, 0, TabCloseScope::All) == 4);
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.empty());
  MICRONOTES_REQUIRE(ui.state.selection().noteId.empty());
}

// Pinning already means "this one stays" -- it is the tab the ceiling will not
// evict and a browse will not take over -- so a bulk close that took a pinned
// tab anyway would make the pin mean nothing at the moment it mattered most.
MICRONOTES_TEST(tab_strip_a_bulk_close_spares_the_pinned_tabs) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-pinned", 5);
  ui.state.editWorkspace().tabs[3].pinned = true;
  MICRONOTES_REQUIRE(closeTabs(ui, 0, TabCloseScope::All) == 4);
  MICRONOTES_REQUIRE(titlesOf(ui) == "Note 3");
  MICRONOTES_REQUIRE(ui.state.workspace().activeTab == 0);
}

MICRONOTES_TEST(tab_strip_a_bulk_close_with_nothing_to_take_changes_nothing) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-close-nothing", 3);
  // The last tab has nothing to its right, and an index past the end names no
  // tab at all.
  MICRONOTES_REQUIRE(closeTabs(ui, 2, TabCloseScope::ToRight) == 0);
  MICRONOTES_REQUIRE(closeTabs(ui, 99, TabCloseScope::All) == 0);
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 3);
}

// The complaint this fixes: leave one note half way down, go to another, come
// back, and be where you were. It used to be wrong in two directions at once --
// switching by the tab strip carried the offset over from the note you left,
// and switching by the sidebar reset it to the top.
MICRONOTES_TEST(tab_strip_each_tab_remembers_where_its_note_was_left) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-tab-scroll", 3);
  const auto tabs = ui.state.workspace().tabs;

  const auto goTo = [&ui](const std::string& noteId) {
    micronotes::tests::require(micronotes::app::saveCurrent(ui, true), "the note did not save");
    ui.state.selectNote(noteId);
    micronotes::app::loadSelectedIntoEditor(ui);
  };

  // `restoreScroll`, not `setScroll`: nothing has laid a page out here, so every
  // pane's ceiling is still zero and the clamping way in would put every offset
  // below straight back to the top. This is the same door the restore itself
  // uses, and for the same reason -- see `ui::ScrollList::restore`.
  goTo(tabs[0].noteId);
  ui.livePage.restoreScroll(240);
  ui.readingPage.restoreScroll(180);
  ui.raw.list.restore(12);

  goTo(tabs[1].noteId);
  // A note arrived at for the first time opens at its own top rather than
  // inheriting where the last one was left.
  MICRONOTES_REQUIRE(ui.livePage.scroll() == 0);
  MICRONOTES_REQUIRE(ui.readingPage.scroll() == 0);
  MICRONOTES_REQUIRE(ui.raw.list.scroll() == 0);
  ui.livePage.restoreScroll(60);

  goTo(tabs[0].noteId);
  MICRONOTES_REQUIRE(ui.livePage.scroll() == 240);
  MICRONOTES_REQUIRE(ui.readingPage.scroll() == 180);
  MICRONOTES_REQUIRE(ui.raw.list.scroll() == 12);

  goTo(tabs[1].noteId);
  MICRONOTES_REQUIRE(ui.livePage.scroll() == 60);
}

// A reload of the note already open is not a move, so it must not touch the
// view: the reader has not gone anywhere.
MICRONOTES_TEST(tab_strip_reloading_the_open_note_leaves_the_view_alone) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-tab-scroll-reload", 2);
  ui.livePage.restoreScroll(150);
  micronotes::app::loadSelectedIntoEditor(ui);
  MICRONOTES_REQUIRE(ui.livePage.scroll() == 150);
}

// Closing a tab takes its remembered place with it, so reopening the note
// starts at its top rather than wherever it was left in a previous life.
MICRONOTES_TEST(tab_strip_a_closed_tab_forgets_where_it_was) {
  UiRuntime ui;
  const OpenNotes notes(ui, "micronotes-tab-scroll-close", 2);
  const auto tabs = ui.state.workspace().tabs;
  const auto goTo = [&ui](const std::string& noteId) {
    micronotes::tests::require(micronotes::app::saveCurrent(ui, true), "the note did not save");
    ui.state.selectNote(noteId);
    micronotes::app::loadSelectedIntoEditor(ui);
  };

  goTo(tabs[0].noteId);
  ui.livePage.restoreScroll(200);
  // Leaving it is what writes the offset onto its tab.
  goTo(tabs[1].noteId);
  // Closing everything but the second tab takes the first one, and its place
  // with it.
  MICRONOTES_REQUIRE(closeTabs(ui, 1, TabCloseScope::Others) == 1);
  goTo(tabs[0].noteId);
  MICRONOTES_REQUIRE(ui.livePage.scroll() == 0);
}

// A note that arrived without front matter gets a permanent id the first time
// it is saved, and everything naming the old one is re-pointed. The buffer's
// own record of which note is in it used to be missed, which left the shell
// believing the note on screen was a different note from the one it loaded --
// and the tab holding that note's remembered place unfindable.
MICRONOTES_TEST(tab_strip_a_first_save_keeps_the_loaded_note_findable) {
  UiRuntime ui;
  const micronotes::tests::TempDir dir("micronotes-tab-adopted-id");
  std::filesystem::create_directories(dir.path());
  // Written by hand, so it has no front matter and so no id of its own yet.
  {
    std::ofstream out(dir.path() / "Handwritten.md");
    out << "# Handwritten\n\nSome body text.\n";
  }
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, dir.path()));
  const auto notes = ui.state.currentNotes();
  MICRONOTES_REQUIRE(notes.size() == 1);
  ui.state.selectNote(notes[0].id);
  micronotes::app::loadSelectedIntoEditor(ui);

  ui.livePage.restoreScroll(300);
  ui.editor.insert("edited");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  // The save gave the note a permanent id; the record of what is loaded moved
  // with it, so the tab it names is still the tab it is in.
  MICRONOTES_REQUIRE(ui.loadedNoteId == ui.state.selection().noteId);
  const auto index = ui.state.workspace().findTab(ui.loadedNoteId);
  MICRONOTES_REQUIRE(index != std::string::npos);

  // Which is what lets the place it was left be put away when it is left.
  micronotes::app::createNote(ui, "Somewhere else");
  MICRONOTES_REQUIRE(ui.state.workspace().tabs[index].liveScroll == 300);
}
