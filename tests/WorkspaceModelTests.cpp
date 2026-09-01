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
