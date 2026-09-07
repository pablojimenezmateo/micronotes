#include "TestSupport.h"

#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"

#include <cmath>

using micronotes::ui::computeShellLayout;
using micronotes::ui::LayoutMode;
using micronotes::ui::resolveLayoutMode;
using micronotes::ui::ShellLayout;
using micronotes::ui::Rect;
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
  ShellLayoutInputs inputs = wideShell();
  inputs.rightPanelVisible = true;
  const ShellLayout layout = computeShellLayout(inputs);
  // The sidebar reaches the window's own leading edge. It used to start at the
  // icon rail's width, which was the one column nothing could take room from.
  MICRONOTES_REQUIRE(nearlyEqual(layout.sidebar.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.content.x,
                                 layout.sidebar.x + layout.sidebar.w +
                                   micronotes::ui::kDividerThickness));
  MICRONOTES_REQUIRE(nearlyEqual(layout.rightPanel.x,
                                 layout.content.x + layout.content.w +
                                   micronotes::ui::kDividerThickness));
  MICRONOTES_REQUIRE(nearlyEqual(layout.rightPanel.x + layout.rightPanel.w, 1600.0f));
  // The panes hang between the menu bar and the status bar, meeting both.
  MICRONOTES_REQUIRE(nearlyEqual(layout.sidebar.y, micronotes::ui::kMenuBarHeight));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.y, layout.sidebar.y + layout.sidebar.h));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.w, 1600.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.status.h, micronotes::ui::kStatusBarHeight));
}

// The menu bar spans the whole window above every panel, because the window
// controls at its right end have to reach the actual corner.
MICRONOTES_TEST(shell_layout_menu_bar_spans_the_window_above_every_panel) {
  const ShellLayout layout = computeShellLayout(wideShell());
  MICRONOTES_REQUIRE(nearlyEqual(layout.menuBar.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.menuBar.y, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.menuBar.w, 1600.0f));
  MICRONOTES_REQUIRE(nearlyEqual(layout.menuBar.h, micronotes::ui::kMenuBarHeight));
  // Nothing starts above the bottom of it.
  for(const auto& region : {layout.sidebar, layout.tabs, layout.breadcrumb, layout.content,
                            layout.rightPanel}) {
    MICRONOTES_REQUIRE(region.y >= layout.menuBar.y + layout.menuBar.h - 0.001f);
  }
}

// The tab strip and the breadcrumb belong to the page's column, not to the
// window. A window-wide strip put the tabs of the note being read over the tree
// that is not being read, and made the band above the sidebar change owner
// depending on what happened to be open.
MICRONOTES_TEST(shell_layout_tabs_and_breadcrumb_stay_inside_the_page_column) {
  ShellLayoutInputs inputs = wideShell();
  inputs.tabStripVisible = true;
  inputs.rightPanelVisible = true;
  const ShellLayout layout = computeShellLayout(inputs);
  for(const auto& band : {layout.tabs, layout.breadcrumb}) {
    MICRONOTES_REQUIRE(nearlyEqual(band.x, layout.content.x));
    MICRONOTES_REQUIRE(nearlyEqual(band.w, layout.content.w));
    // Clear of both panels, which run the full height beside them.
    MICRONOTES_REQUIRE(band.x >= layout.sidebar.x + layout.sidebar.w - 0.001f);
    MICRONOTES_REQUIRE(band.x + band.w <= layout.rightPanel.x + 0.001f);
  }
  // Stacked: tabs, then the breadcrumb, then the page.
  MICRONOTES_REQUIRE(nearlyEqual(layout.breadcrumb.y, layout.tabs.y + layout.tabs.h));
  MICRONOTES_REQUIRE(nearlyEqual(layout.content.y,
                                 layout.breadcrumb.y + layout.breadcrumb.h +
                                   micronotes::ui::kDividerThickness));
}

// The breadcrumb is reserved whether or not a note is open, so a hit test
// against the page agrees with what was drawn. It is where the note's own
// identity lives, and a band that appears and disappears would move the page
// under the reader every time a tab closed.
MICRONOTES_TEST(shell_layout_breadcrumb_is_always_reserved) {
  for(const bool tabs : {true, false}) {
    ShellLayoutInputs inputs = wideShell();
    inputs.tabStripVisible = tabs;
    const ShellLayout layout = computeShellLayout(inputs);
    MICRONOTES_REQUIRE(nearlyEqual(layout.breadcrumb.h, micronotes::ui::kBreadcrumbHeight));
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
  // With the sidebar away the page starts at the window's edge: there is no
  // rail left to keep a column for, and a hidden panel takes no divider either.
  MICRONOTES_REQUIRE(nearlyEqual(hidden.content.x, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(hidden.content.x + hidden.content.w, 1600.0f));
}

// Whatever the window is doing and whichever panels are showing, no region is
// ever handed a negative size and none of them overlaps another. This was the
// invariant the icon rail's own test carried, and it outlives the rail.
MICRONOTES_TEST(shell_layout_never_hands_a_region_a_negative_size) {
  const float widths[] = {320.0f, 700.0f, 1000.0f, 1600.0f, 3840.0f};
  for(const float width : widths) {
    ShellLayoutInputs inputs = wideShell();
    inputs.windowWidth = width;
    for(const bool sidebar : {true, false}) {
      for(const bool right : {true, false}) {
        for(const bool tabs : {true, false}) {
          inputs.sidebarVisible = sidebar;
          inputs.rightPanelVisible = right;
          inputs.tabStripVisible = tabs;
          const ShellLayout layout = computeShellLayout(inputs);
          for(const auto& region : {layout.menuBar, layout.sidebar, layout.tabs,
                                    layout.breadcrumb, layout.content, layout.rightPanel,
                                    layout.status}) {
            MICRONOTES_REQUIRE(region.w >= 0.0f);
            MICRONOTES_REQUIRE(region.h >= 0.0f);
          }
          MICRONOTES_REQUIRE(layout.content.x >= layout.sidebar.x + layout.sidebar.w - 0.001f);
        }
      }
    }
  }
}

MICRONOTES_TEST(shell_layout_right_panel_takes_room_only_when_shown) {
  ShellLayoutInputs inputs = wideShell();
  const ShellLayout without = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(without.rightPanel.w, 0.0f));

  inputs.rightPanelVisible = true;
  const ShellLayout with = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(with.rightPanel.w, micronotes::ui::kDefaultRightPanelWidth));
    // The panel's width *and* the rule between it and the page, which belongs to
  // neither of them.
  MICRONOTES_REQUIRE(nearlyEqual(with.content.w,
                                 without.content.w - micronotes::ui::kDefaultRightPanelWidth -
                                   micronotes::ui::kDividerThickness));
  // The panel the other side of the page is unmoved by it.
  MICRONOTES_REQUIRE(with.sidebar == without.sidebar);
}

MICRONOTES_TEST(shell_layout_tab_strip_pushes_the_page_down) {
  ShellLayoutInputs inputs = wideShell();
  const ShellLayout without = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(without.tabs.h, 0.0f));
  MICRONOTES_REQUIRE(nearlyEqual(without.content.y,
                                 micronotes::ui::kMenuBarHeight +
                                   micronotes::ui::kBreadcrumbHeight +
                                   micronotes::ui::kDividerThickness));

  inputs.tabStripVisible = true;
  const ShellLayout with = computeShellLayout(inputs);
  MICRONOTES_REQUIRE(nearlyEqual(with.tabs.h, micronotes::ui::kTabStripHeight));
  MICRONOTES_REQUIRE(nearlyEqual(with.tabs.y, micronotes::ui::kMenuBarHeight));
  MICRONOTES_REQUIRE(nearlyEqual(with.content.y,
                                 with.breadcrumb.y + with.breadcrumb.h +
                                   micronotes::ui::kDividerThickness));
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

// The page inside a pane, and the measure on it. Both live here rather than in
// each surface that draws a page because three of them do -- the live page, the
// reading pane and the raw pane -- and the inset used to be written out seven
// times.
MICRONOTES_TEST(page_rect_is_inset_on_all_four_sides_and_lower_at_the_foot) {
  const Rect page = micronotes::ui::pageRectIn({100.0f, 50.0f, 800.0f, 600.0f});
  MICRONOTES_REQUIRE(page.x == 100.0f + micronotes::ui::kPagePad);
  MICRONOTES_REQUIRE(page.y == 50.0f + micronotes::ui::kPagePad);
  MICRONOTES_REQUIRE(page.w == 800.0f - micronotes::ui::kPagePad * 2.0f);
  // Deeper at the foot, so the page stops clear of the status bar.
  MICRONOTES_REQUIRE(page.h < 600.0f - micronotes::ui::kPagePad * 2.0f);
  MICRONOTES_REQUIRE(page.y + page.h < 50.0f + 600.0f);
}

MICRONOTES_TEST(page_rect_never_reports_a_negative_size) {
  // A pane squeezed smaller than its own padding hands back an empty page
  // rather than a negative one every hit test downstream would have to defend
  // against.
  const Rect page = micronotes::ui::pageRectIn({0.0f, 0.0f, 4.0f, 4.0f});
  MICRONOTES_REQUIRE(page.w == 0.0f);
  MICRONOTES_REQUIRE(page.h == 0.0f);
}

MICRONOTES_TEST(page_column_is_centred_and_capped_at_the_reader_page_width) {
  const Rect page = micronotes::ui::pageRectIn({0.0f, 0.0f, 1400.0f, 900.0f});
  const auto column = micronotes::ui::pageColumnIn(page, 0.0f);
  // Extra width becomes margin, not more characters per line.
  MICRONOTES_REQUIRE(column.width == micronotes::ui::pageWidthPx());
  const float leftMargin = column.left - page.x;
  const float rightMargin = page.x + page.w - (column.left + column.width);
  MICRONOTES_REQUIRE(std::abs(leftMargin - rightMargin) <= 1.0f);
}

MICRONOTES_TEST(page_column_gives_the_gutter_its_room_before_it_centres) {
  // Narrow enough that a centred column would start inside the gutter. The
  // gutter wins: the live page's insert, drag and fold handles have to land
  // somewhere.
  const Rect page = micronotes::ui::pageRectIn({0.0f, 0.0f, 420.0f, 900.0f});
  const float gutter = 78.0f;
  const auto centred = micronotes::ui::pageColumnIn(page, 0.0f);
  MICRONOTES_REQUIRE(centred.left - page.x < gutter);
  const auto shifted = micronotes::ui::pageColumnIn(page, gutter);
  MICRONOTES_REQUIRE(shifted.left == page.x + gutter);
  MICRONOTES_REQUIRE(shifted.width < centred.width);
  // And it still ends inside the page.
  MICRONOTES_REQUIRE(shifted.left + shifted.width <= page.x + page.w + 0.001f);
}

MICRONOTES_TEST(page_column_matches_between_a_gutterless_surface_and_a_wide_page) {
  // The live page and the reading pane draw the same note. On any page wide
  // enough to centre the measure clear of the gutter they must agree on it
  // exactly, or switching panes re-wraps every line.
  const Rect page = micronotes::ui::pageRectIn({44.0f, 30.0f, 1100.0f, 900.0f});
  MICRONOTES_REQUIRE(micronotes::ui::pageColumnIn(page, 78.0f) ==
                     micronotes::ui::pageColumnIn(page, 0.0f));
}
