#include "TestSupport.h"

#include "ui/Metrics.h"
#include "ui/ShellLayout.h"

using micronotes::ui::computeShellLayout;
using micronotes::ui::LayoutMode;
using micronotes::ui::resolveLayoutMode;
using micronotes::ui::ShellLayout;
using micronotes::ui::ShellLayoutInputs;

namespace {

// A roomy window with the three-column shell every existing library restores
// into, so a test only has to say what it is changing.
ShellLayoutInputs wideShell() {
  ShellLayoutInputs inputs;
  inputs.windowWidth = 1600.0f;
  inputs.windowHeight = 900.0f;
  inputs.sidebarWidth = micronotes::ui::kDefaultSidebarWidth;
  inputs.rightPanelWidth = micronotes::ui::kDefaultRightPanelWidth;
  return inputs;
}

bool nearlyEqual(float a, float b) {
  return (a > b ? a - b : b - a) < 0.001f;
}

}

MICRONOTES_TEST(shell_layout_panes_tile_the_window_without_a_gap) {
  const ShellLayout layout = computeShellLayout(wideShell());
  MICRONOTES_REQUIRE(nearlyEqual(layout.sidebar.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.content.x, layout.sidebar.x + layout.sidebar.w));
  MICRONOTES_REQUIRE(nearlyEqual(layout.rightPanel.x, layout.content.x + layout.content.w));
  MICRONOTES_REQUIRE(nearlyEqual(layout.rightPanel.x + layout.rightPanel.w, 1600.0f));
  // The panes hang between the title bar and the status bar, meeting both.
  MICRONOTES_REQUIRE(nearlyEqual(layout.sidebar.y, micronotes::ui::kTitleBarHeight));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.y, layout.sidebar.y + layout.sidebar.h));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.w, 1600.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.h, micronotes::ui::kStatusBarHeight));
}

// The title bar spans the whole window above every panel, because the window
// controls at its right end have to reach the actual corner.
MICRONOTES_TEST(shell_layout_title_bar_spans_the_window_above_every_panel) {
  const ShellLayout layout = computeShellLayout(wideShell());
  MICRONOTES_REQUIRE(nearlyEqual(layout.titleBar.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.titleBar.y, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.titleBar.w, 1600.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.titleBar.h, micronotes::ui::kTitleBarHeight));
  // Nothing starts above the bottom of it.
  for(const auto& region : {layout.sidebar, layout.tabs, layout.content, layout.rightPanel}) {
    MICRONOTES_REQUIRE(region.y >= layout.titleBar.y + layout.titleBar.h - 0.001f);
  }
}

// A hidden panel gives its room to the page rather than leaving a hole, and
// comes back empty rather than absent so a caller can hit-test it either way.
MICRONOTES_TEST(shell_layout_hidden_panels_give_their_room_to_the_page) {
  ShellLayoutInputs inputs = wideShell();
  const float withPanels = computeShellLayout(inputs).content.w;

  inputs.sidebarVisible = false;
  const ShellLayout hidden = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(hidden.sidebar.w, 0.0f));
  MICRONOTES_REQUIRE(hidden.content.w > withPanels);
  MICRONOTES_REQUIRE(nearlyEqual(hidden.content.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(hidden.content.x + hidden.content.w, 1600.0f));
}

MICRONOTES_TEST(shell_layout_right_panel_takes_room_only_when_shown) {
  ShellLayoutInputs inputs = wideShell();
  const ShellLayout without = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(without.rightPanel.w, 0.0f));

  inputs.rightPanelVisible = true;
  const ShellLayout with = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(with.rightPanel.w, micronotes::ui::kDefaultRightPanelWidth));
  MICRONOTES_REQUIRE(nearlyEqual(with.content.w, without.content.w - micronotes::ui::kDefaultRightPanelWidth));
  // The panel the other side of the page is unmoved by it.
  MICRONOTES_REQUIRE(with.sidebar == without.sidebar);
}

MICRONOTES_TEST(shell_layout_tab_strip_pushes_the_page_down) {
  ShellLayoutInputs inputs = wideShell();
  const ShellLayout without = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(without.tabs.h, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(without.content.y, micronotes::ui::kTitleBarHeight));

  inputs.tabStripVisible = true;
  const ShellLayout with = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(with.tabs.h, micronotes::ui::kTabStripHeight));
  MICRONOTES_REQUIRE(nearlyEqual(with.tabs.y, micronotes::ui::kTitleBarHeight));
  MICRONOTES_REQUIRE(nearlyEqual(with.content.y, with.tabs.y + with.tabs.h));
  // The page loses exactly the strip's height, and nothing runs past the status bar.
  MICRONOTES_REQUIRE(nearlyEqual(with.content.h, without.content.h - micronotes::ui::kTabStripHeight));
  MICRONOTES_REQUIRE(nearlyEqual(with.content.y + with.content.h, with.status.y));
}

// A window too narrow for every minimum at once still leaves the page usable:
// the panels give room back, and none of them ends up with a negative width.
MICRONOTES_TEST(shell_layout_narrow_window_keeps_the_page_usable) {
  ShellLayoutInputs inputs = wideShell();
  inputs.rightPanelVisible = true;
  inputs.windowWidth = 700.0f;
  const ShellLayout layout = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(layout.sidebar.w > 0.0f);
  MICRONOTES_REQUIRE(layout.rightPanel.w > 0.0f);
  MICRONOTES_REQUIRE(layout.content.w > 0.0f);
  MICRONOTES_REQUIRE(layout.mode == LayoutMode::Compact);
}

// A panel dragged wide on a big screen and then persisted must not swallow a
// smaller one on the next launch.
MICRONOTES_TEST(shell_layout_clamps_a_panel_to_its_share_of_the_window) {
  ShellLayoutInputs inputs = wideShell();
  inputs.sidebarWidth = 900.0f;
  const ShellLayout layout = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(layout.sidebar.w <= 1600.0f * micronotes::ui::kMaxSidebarFraction + 0.001f);
  MICRONOTES_REQUIRE(layout.content.w >= micronotes::ui::kMinContentWidth);
}

// The mode a window settles in depends on the mode it was already in, so a
// slow drag across the breakpoint cannot flip the panels back and forth.
MICRONOTES_TEST(shell_layout_compact_breakpoint_has_hysteresis) {
  const float enter = micronotes::ui::kCompactBreakpoint;
  const float band = micronotes::ui::kCompactHysteresis;
  MICRONOTES_REQUIRE(resolveLayoutMode(enter - 1.0f, LayoutMode::Regular) == LayoutMode::Compact);
  MICRONOTES_REQUIRE(resolveLayoutMode(enter + 1.0f, LayoutMode::Regular) == LayoutMode::Regular);
  // Inside the band, whichever mode is in force stays in force.
  MICRONOTES_REQUIRE(resolveLayoutMode(enter + 1.0f, LayoutMode::Compact) == LayoutMode::Compact);
  MICRONOTES_REQUIRE(resolveLayoutMode(enter + band, LayoutMode::Compact) == LayoutMode::Regular);
}

// The memo key is the whole point of the inputs struct: two frames that agree
// on every field must produce the same rects, and a frame that differs by one
// field must be unequal so the memo misses.
MICRONOTES_TEST(shell_layout_inputs_compare_by_value) {
  const ShellLayoutInputs a = wideShell();
  ShellLayoutInputs b = wideShell();
  MICRONOTES_REQUIRE(a == b);
  MICRONOTES_REQUIRE(computeShellLayout(a) == computeShellLayout(b));
  b.rightPanelVisible = true;
  MICRONOTES_REQUIRE(!(a == b));
  MICRONOTES_REQUIRE(!(computeShellLayout(a) == computeShellLayout(b)));
}
