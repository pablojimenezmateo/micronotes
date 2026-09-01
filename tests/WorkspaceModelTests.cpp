#include "TestSupport.h"

#include "ui/ShellLayout.h"
#include "ui/WorkspaceModel.h"

using micronotes::ui::LayoutMode;
using micronotes::ui::RightPanelView;
using micronotes::ui::rightPanelViewFromName;
using micronotes::ui::rightPanelViewName;
using micronotes::ui::WorkspaceModel;
using micronotes::ui::computeShellLayout;

MICRONOTES_TEST(workspace_starts_with_the_two_navigating_panels_open) {
  const WorkspaceModel workspace;
  MICRONOTES_REQUIRE(workspace.sidebarVisible);
  MICRONOTES_REQUIRE(workspace.noteListVisible);
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

// Hiding both navigating panels would leave the palette as the only way to
// reach another note, so the last one standing refuses to go.
MICRONOTES_TEST(workspace_keeps_one_way_to_reach_another_note) {
  WorkspaceModel workspace;
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(!workspace.sidebarVisible);
  MICRONOTES_REQUIRE(!workspace.togglePanel(&WorkspaceModel::noteListVisible));
  MICRONOTES_REQUIRE(workspace.noteListVisible);

  // The right panel is not one of the two, so it may still be hidden with only
  // one navigating panel open.
  workspace.rightPanelVisible = true;
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::rightPanelVisible));
  MICRONOTES_REQUIRE(!workspace.rightPanelVisible);

  // Bringing the sidebar back frees the note list to go.
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::sidebarVisible));
  MICRONOTES_REQUIRE(workspace.togglePanel(&WorkspaceModel::noteListVisible));
  MICRONOTES_REQUIRE(!workspace.noteListVisible);
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
  for(const auto view : {RightPanelView::Outline, RightPanelView::Tags}) {
    MICRONOTES_REQUIRE(rightPanelViewFromName(rightPanelViewName(view)) == view);
  }
  // A name from a newer version, or a typo, falls back rather than failing.
  MICRONOTES_REQUIRE(rightPanelViewFromName("graph") == RightPanelView::Outline);
  MICRONOTES_REQUIRE(rightPanelViewFromName("") == RightPanelView::Outline);
}

namespace {

WorkspaceModel withTabs(std::initializer_list<const char*> ids) {
  WorkspaceModel workspace;
  for(const char* id : ids) workspace.tabs.push_back({id, micronotes::ui::PaneMode::Live, false});
  return workspace;
}

}

// A single click in the sidebar reuses the tab you are in. That is what stops
// an afternoon of reading from leaving thirty tabs behind.
MICRONOTES_TEST(workspace_opening_a_note_replaces_the_active_tab) {
  WorkspaceModel workspace;
  workspace.openNote("a", false);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 1);
  workspace.openNote("b", false);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 1);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "b");
}

MICRONOTES_TEST(workspace_opening_in_a_new_tab_keeps_the_old_one) {
  WorkspaceModel workspace;
  workspace.openNote("a", false);
  workspace.openNote("b", true);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "a");
  MICRONOTES_REQUIRE(workspace.tabs[1].noteId == "b");
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
}

// Clicking a note that is already open is a request to go to it, never to open
// a second tab showing the same thing.
MICRONOTES_TEST(workspace_opening_a_note_that_is_open_goes_to_it) {
  auto workspace = withTabs({"a", "b", "c"});
  workspace.activeTab = 2;
  workspace.openNote("a", false);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 3);
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  // Even when a new tab was asked for.
  workspace.openNote("b", true);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 3);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
}

// A pin is a promise that this tab stays; honouring it means opening a new one
// rather than quietly ignoring the pin.
MICRONOTES_TEST(workspace_a_pinned_tab_is_never_replaced) {
  WorkspaceModel workspace;
  workspace.openNote("a", false);
  workspace.tabs[0].pinned = true;
  workspace.openNote("b", false);
  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "a");
  MICRONOTES_REQUIRE(workspace.tabs[1].noteId == "b");
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
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Live);
  workspace.activeTab = 0;
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Viewer);

  // With nothing open there is still an answer, so no caller has to ask whether
  // there is a tab before asking how it is being shown.
  WorkspaceModel empty;
  MICRONOTES_REQUIRE(empty.paneMode() == micronotes::ui::PaneMode::Live);
  empty.setPaneMode(micronotes::ui::PaneMode::Split);
  MICRONOTES_REQUIRE(empty.paneMode() == micronotes::ui::PaneMode::Live);
}

// A new tab inherits how you were reading the last one.
MICRONOTES_TEST(workspace_a_new_tab_inherits_the_pane_mode) {
  WorkspaceModel workspace;
  workspace.openNote("a", false);
  workspace.setPaneMode(micronotes::ui::PaneMode::Viewer);
  workspace.openNote("b", true);
  MICRONOTES_REQUIRE(workspace.paneMode() == micronotes::ui::PaneMode::Viewer);
}
