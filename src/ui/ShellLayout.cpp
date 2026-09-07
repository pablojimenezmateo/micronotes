#include "ui/ShellLayout.h"

#include "ui/Metrics.h"
#include "ui/Settings.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

// A panel that is showing is clamped between its minimum and its share of the
// window; a panel that is not takes no room at all.
float panelWidth(bool visible, float requested, float minimum, float fraction, float usableWidth) {
  if(!visible) return 0.0f;
  const float ceiling = std::max(minimum, usableWidth * fraction);
  return std::clamp(requested, minimum, ceiling);
}

}

LayoutMode resolveLayoutMode(float windowWidth, LayoutMode previous) {
  // The band belongs to whichever mode is already in force: leaving compact
  // costs a few more pixels than entering it did. Without that, a window
  // dragged slowly across the breakpoint flips modes on every motion event and
  // the panels visibly stutter.
  const float enter = kCompactBreakpoint;
  const float leave = kCompactBreakpoint + kCompactHysteresis;
  if(previous == LayoutMode::Compact) return windowWidth >= leave ? LayoutMode::Regular : LayoutMode::Compact;
  return windowWidth < enter ? LayoutMode::Compact : LayoutMode::Regular;
}

ShellLayout computeShellLayout(const ShellLayoutInputs& inputs) {
  ShellLayout layout;

  const float window = std::max(inputs.windowWidth, kMinUsableWidth);
  // The mode is a question about the window, so it is asked of the window; the
  // panel arithmetic below is a question about the room left over. Those used to
  // be different numbers -- the icon rail's width was spoken for before anything
  // else had a say -- and with the rail gone the sidebar starts at the window's
  // own leading edge and the two questions have the same answer.
  layout.mode = resolveLayoutMode(window, inputs.previousMode);
  const float usable = std::max(kMinContentWidth, window);

  float sidebar = inputs.sidebarWidth;
  if(layout.mode == LayoutMode::Compact) sidebar = kCompactSidebarWidth;

  sidebar = panelWidth(inputs.sidebarVisible, sidebar, kMinSidebarWidth, kMaxSidebarFraction, usable);
  float right = panelWidth(inputs.rightPanelVisible, inputs.rightPanelWidth, kMinRightPanelWidth,
                           kMaxRightPanelFraction, usable);

  // Whatever the panels asked for, the page keeps kMinContentWidth. The panels
  // give the difference back in the order they are least missed: the right
  // panel is a reference, and the sidebar is how you get anywhere at all.
  const auto overflow = [&] {
    return sidebar + right + kMinContentWidth - usable;
  };
  const auto squeeze = [&](float& width, bool visible, float floorWidth) {
    const float over = overflow();
    if(over <= 0.0f || !visible) return;
    width = std::max(floorWidth, width - over);
  };
  squeeze(right, inputs.rightPanelVisible, kRightPanelSqueezeFloor);
  squeeze(sidebar, inputs.sidebarVisible, kSidebarSqueezeFloor);

  // The menu bar spans the window above everything, and the status bar spans it
  // below: both hold controls that have to reach the actual corners. Every
  // other region lives in the band between them.
  const float bodyY = kMenuBarHeight;
  const float paneBottom = inputs.windowHeight - kStatusBarHeight;
  const float paneHeight = std::max(0.0f, paneBottom - bodyY);
  // A panel that is showing takes a divider's worth of room as well, so the
  // rule between it and the page belongs to neither and overlaps nothing.
  const float contentX = sidebar + (inputs.sidebarVisible ? kDividerThickness : 0.0f);
  const float rightReserve = right + (inputs.rightPanelVisible ? kDividerThickness : 0.0f);
  // Floored at zero. On a window too narrow to hold both panels and a page at
  // once, the panels overhang rather than the page being handed a negative
  // width that every hit test downstream would have to defend against.
  const float contentW = std::max(0.0f, inputs.windowWidth - contentX - rightReserve);
  const float tabsH = inputs.tabStripVisible ? kTabStripHeight : 0.0f;

  layout.menuBar = {0.0f, 0.0f, inputs.windowWidth, kMenuBarHeight};
  layout.sidebar = {0.0f, bodyY, sidebar, paneHeight};
  layout.tabs = {contentX, bodyY, contentW, tabsH};
  layout.breadcrumb = {contentX, bodyY + tabsH, contentW, kBreadcrumbHeight};
  const float contentY = layout.breadcrumb.y + layout.breadcrumb.h + kDividerThickness;
  layout.content = {contentX, contentY, contentW,
                    std::max(0.0f, paneBottom - contentY)};
  layout.rightPanel = {contentX + contentW + (inputs.rightPanelVisible ? kDividerThickness : 0.0f),
                       bodyY, right, paneHeight};
  layout.status = {0.0f, paneBottom, inputs.windowWidth, kStatusBarHeight};
  return layout;
}

Rect pageRectIn(Rect pane) {
  return {pane.x + kPagePad, pane.y + kPagePad, std::max(0.0f, pane.w - kPagePad * 2.0f),
          std::max(0.0f, pane.h - kPagePad * 2.0f - kPageBottomPad)};
}

PageColumn pageColumnIn(Rect page, float gutter) {
  PageColumn column;
  const float available = std::max(kPageMinColumn, page.w - kPageColumnPad);
  column.width = std::min(available, pageWidthPx());
  column.left = page.x + std::round((page.w - column.width) / 2.0f);
  // Centring would put the column's left edge inside the gutter, so it stops
  // being centred: the gutter keeps its room and the column takes the rest.
  if(column.left < page.x + gutter) {
    column.width = std::max(kPageMinColumn, page.w - gutter - kPageColumnPad / 2.0f);
    column.left = page.x + gutter;
  }
  return column;
}

}
