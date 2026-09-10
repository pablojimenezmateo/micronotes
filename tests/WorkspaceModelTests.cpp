#include "TestSupport.h"

#include "ui/ShellLayout.h"
#include "ui/WorkspaceModel.h"

using micronotes::ui::LayoutMode;
using micronotes::ui::RightPanelView;
using micronotes::ui::rightPanelViewFromName;
using micronotes::ui::rightPanelViewName;
using micronotes::ui::WorkspaceModel;
using micronotes::ui::computeShellLayout;

MICRONOTES_TEST(workspace_starts_with_the_navigating_panel_open) {
  const WorkspaceModel workspace;
  MICRONOTES_REQUIRE(workspace.sidebarVisible);
  // The right panel describes the open note, so it is not there until asked for.
  MICRONOTES_REQUIRE(!workspace.rightPanelVisible);
}

MICRONOTES_TEST(workspace_toggles_a_panel_both_ways) {
  WorkspaceModel workspace;
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::rightPanelVisible));
  MICRONOTES_REQUIRE(workspace.rightPanelVisible);
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::rightPanelVisible));
  MICRONOTES_REQUIRE(!workspace.rightPanelVisible);
}

// The sidebar is the only navigator, and hiding it is allowed: it is one key,
// the same key brings it back, and the palette and the tab strip are still
// there meanwhile. What must not happen is a toggle that only goes one way.
MICRONOTES_TEST(workspace_sidebar_hides_and_comes_back) {
  WorkspaceModel workspace;
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(!workspace.sidebarVisible);
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(workspace.sidebarVisible);

  // Both panels away at once is a reachable state, and still reversible.
  workspace.rightPanelVisible = true;
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::rightPanelVisible));
  MICRONOTES_REQUIRE(!workspace.sidebarVisible);
  MICRONOTES_REQUIRE(!workspace.rightPanelVisible);
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(workspace.sidebarVisible);
}

// A hidden panel keeps its width, so showing it again restores the size it had.
MICRONOTES_TEST(workspace_hidden_panel_keeps_its_width) {
  WorkspaceModel workspace;
  workspace.sidebarWidth = 315.0f;
  workspace.togglePanel(&WorkspaceModel::sidebarVisible);
  const auto hidden = workspace.layoutInputs(1600.0f, 900.0f, LayoutMode::Regular);
  MICRONOTES_REQUIRE(!hidden.sidebarVisible);
  MICRONOTES_REQUIRE(hidden.sidebarWidth == 315.0f);
  MICRONOTES_REQUIRE(computeShellLayout(hidden).sidebar.w == 0.0f);

  workspace.togglePanel(&WorkspaceModel::sidebarVisible);
  MICRONOTES_REQUIRE(computeShellLayout(
    workspace.layoutInputs(1600.0f, 900.0f, LayoutMode::Regular)).sidebar.w == 315.0f);
}

MICRONOTES_TEST(workspace_layout_inputs_carry_the_whole_arrangement) {
  WorkspaceModel workspace;
  workspace.rightPanelVisible = true;
  workspace.rightPanelWidth = 260.0f;
  const auto inputs = workspace.layoutInputs(1440.0f, 810.0f, LayoutMode::Compact);
  MICRONOTES_REQUIRE(inputs.windowWidth == 1440.0f && inputs.windowHeight == 810.0f);
  MICRONOTES_REQUIRE(inputs.rightPanelVisible && inputs.rightPanelWidth == 260.0f);
  MICRONOTES_REQUIRE(inputs.previousMode == LayoutMode::Compact);
}

// The name is what lands in .micronotes/ui.state, so it has to survive the trip.
MICRONOTES_TEST(workspace_right_panel_view_round_trips_through_its_name) {
  // Every view, not a selection of them: the one left out of this loop was
  // Backlinks, which is also the one with an alias to get wrong.
  for(const auto view : {RightPanelView::Outline, RightPanelView::Backlinks, RightPanelView::Tags}) {
    MICRONOTES_REQUIRE(rightPanelViewFromName(rightPanelViewName(view)) == view);
  }
  // The name the panel's own tab is labelled with, so a view asked for by what
  // is on screen resolves to it.
  MICRONOTES_REQUIRE(rightPanelViewFromName("links") == RightPanelView::Backlinks);
  // A name from a newer version, or a typo, falls back rather than failing.
  MICRONOTES_REQUIRE(rightPanelViewFromName("graph") == RightPanelView::Outline);
  MICRONOTES_REQUIRE(rightPanelViewFromName("") == RightPanelView::Outline);
}

namespace {

WorkspaceModel withTabs(std::initializer_list<const char*> ids) {
  WorkspaceModel workspace;
  for(const char* id : ids) workspace.tabs.push_back({.noteId = id});
  return workspace;
}

}

// Opening a note gives it a tab of its own, and the note you were reading stays
// open beside it.
MICRONOTES_TEST(workspace_opening_a_note_opens_a_tab_on_it) {
  WorkspaceModel workspace;
  workspace.openNote("a");
  MICRONOTES_REQUIRE(workspace.tabs.size() == 1);
  workspace.openNote("b");
  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "a");
  MICRONOTES_REQUIRE(workspace.tabs[1].noteId == "b");
  // Beside the one it came from rather than at the end, so a strip stays in the
  // order the reader built it.
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
}

// The one exception, and the reason it exists: the keyboard cursor walking the
// sidebar opens every note it passes over, so arrowing through a library would
// otherwise be one tab per note in it.
MICRONOTES_TEST(workspace_a_cursor_move_takes_over_the_tab_showing) {
  WorkspaceModel workspace;
  workspace.openNote("a");
  for(const char* id : {"b", "c", "d", "e"}) {
    workspace.openNote(id, micronotes::ui::TabPolicy::Reuse);
  }
  MICRONOTES_REQUIRE(workspace.tabs.size() == 1);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "e");
}

// Clicking a note that is already open is a request to go to it, never to open
// a second tab showing the same thing.
MICRONOTES_TEST(workspace_opening_a_note_that_is_open_goes_to_it) {
  auto workspace = withTabs({"a", "b", "c"});
  workspace.activeTab = 2;
  workspace.openNote("a");
  MICRONOTES_REQUIRE(workspace.tabs.size() == 3);
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  // And a cursor move onto an open note goes to it rather than taking over the
  // tab showing, which would leave the note listed twice.
  workspace.openNote("b", micronotes::ui::TabPolicy::Reuse);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 3);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
}

// A pin is a promise that this tab stays; honouring it means opening a new one
// rather than quietly ignoring the pin. That holds for the cursor too, which is
// the only thing that takes a tab over.
MICRONOTES_TEST(workspace_a_pinned_tab_is_never_replaced) {
  WorkspaceModel workspace;
  workspace.openNote("a");
  workspace.tabs[0].pinned = true;
  workspace.openNote("b", micronotes::ui::TabPolicy::Reuse);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "a");
  MICRONOTES_REQUIRE(workspace.tabs[1].noteId == "b");
}

// Every note in its own tab means the strip grows as you read, so it has to
// stop somewhere. The ceiling takes the oldest tab that nobody has said
// anything about -- never a pinned one, never the one on screen.
MICRONOTES_TEST(workspace_the_strip_stops_growing_at_the_ceiling) {
  WorkspaceModel workspace;
  for(std::size_t i = 0; i < micronotes::ui::kMaxTabs + 10; ++i) {
    workspace.openNote("n" + std::to_string(i));
  }
  MICRONOTES_REQUIRE(workspace.tabs.size() == micronotes::ui::kMaxTabs);
  // The newest survived and is showing; the oldest are the ones that went.
  MICRONOTES_REQUIRE(workspace.tabs.back().noteId == "n" + std::to_string(micronotes::ui::kMaxTabs + 9));
  MICRONOTES_REQUIRE(workspace.activeTab == workspace.tabs.size() - 1);
  MICRONOTES_REQUIRE(workspace.findTab("n0") == std::string::npos);
}

// A pin survives the ceiling, which is what makes pinning the way to keep a
// tab through an afternoon of reading.
MICRONOTES_TEST(workspace_the_ceiling_never_takes_a_pinned_tab) {
  WorkspaceModel workspace;
  workspace.openNote("keep");
  workspace.tabs[0].pinned = true;
  for(std::size_t i = 0; i < micronotes::ui::kMaxTabs + 10; ++i) {
    workspace.openNote("n" + std::to_string(i));
  }
  MICRONOTES_REQUIRE(workspace.tabs.size() == micronotes::ui::kMaxTabs);
  MICRONOTES_REQUIRE(workspace.findTab("keep") != std::string::npos);
  MICRONOTES_REQUIRE(workspace.tabs[workspace.findTab("keep")].pinned);
}

// And a strip of nothing but pins does not get emptied to satisfy the ceiling:
// a ceiling that overrides a pin is a ceiling that loses what was kept.
MICRONOTES_TEST(workspace_a_strip_of_pins_stays_over_the_ceiling) {
  WorkspaceModel workspace;
  for(std::size_t i = 0; i < micronotes::ui::kMaxTabs + 5; ++i) {
    workspace.openNote("p" + std::to_string(i));
    workspace.tabs[workspace.activeTab].pinned = true;
  }
  MICRONOTES_REQUIRE(workspace.tabs.size() == micronotes::ui::kMaxTabs + 5);
  for(const auto& tab : workspace.tabs) MICRONOTES_REQUIRE(tab.pinned);
}

// Closing to the left of what you are reading must not change what you are
// reading, which means the index follows its own tab rather than staying put.
MICRONOTES_TEST(workspace_closing_a_tab_keeps_the_right_one_showing) {
  auto workspace = withTabs({"a", "b", "c"});
  workspace.activeTab = 2;
  workspace.closeTab(0);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
  MICRONOTES_REQUIRE(workspace.tabs[workspace.activeTab].noteId == "c");
}

// Closing the one you are reading moves to the right, and to the left when
// there is no right -- so a run of closes keeps going in one direction.
MICRONOTES_TEST(workspace_closing_the_active_tab_moves_along) {
  auto workspace = withTabs({"a", "b", "c"});
  workspace.activeTab = 1;
  workspace.closeTab(1);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
  MICRONOTES_REQUIRE(workspace.tabs[workspace.activeTab].noteId == "c");
  workspace.closeTab(1);
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  MICRONOTES_REQUIRE(workspace.tabs[workspace.activeTab].noteId == "a");
  workspace.closeTab(0);
  MICRONOTES_REQUIRE(workspace.tabs.empty());
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  // Closing what is not there does nothing rather than reaching past the end.
  workspace.closeTab(0);
  workspace.closeTab(99);
  MICRONOTES_REQUIRE(workspace.tabs.empty());
}

MICRONOTES_TEST(workspace_stepping_between_tabs_wraps) {
  auto workspace = withTabs({"a", "b", "c"});
  workspace.stepTab(1);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
  workspace.stepTab(2);
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  workspace.stepTab(-1);
  MICRONOTES_REQUIRE(workspace.activeTab == 2);
  // One tab has nowhere to step to, and must not look broken by moving.
  auto single = withTabs({"only"});
  single.stepTab(1);
  MICRONOTES_REQUIRE(single.activeTab == 0);
}

// The pane mode describes how you are looking at *this* note. A window-wide one
// is wrong the moment two are open.
MICRONOTES_TEST(workspace_pane_mode_belongs_to_the_tab) {
  auto workspace = withTabs({"a", "b"});
  workspace.setPaneMode(micronotes::ui::PaneMode::Viewer);
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Viewer);
  workspace.activeTab = 1;
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Split);
  workspace.activeTab = 0;
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Viewer);

  // With nothing open there is still an answer, so no caller has to ask whether
  // there is a tab before asking how it is being shown.
  WorkspaceModel empty;
  MICRONOTES_REQUIRE(empty.paneMode() == micronotes::ui::PaneMode::Split);
  empty.setPaneMode(micronotes::ui::PaneMode::Editor);
  MICRONOTES_REQUIRE(empty.paneMode() == micronotes::ui::PaneMode::Split);
}

// A new tab inherits how you were reading the last one.
MICRONOTES_TEST(workspace_a_new_tab_inherits_the_pane_mode) {
  WorkspaceModel workspace;
  workspace.openNote("a");
  workspace.setPaneMode(micronotes::ui::PaneMode::Viewer);
  workspace.openNote("b");
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Viewer);
}
