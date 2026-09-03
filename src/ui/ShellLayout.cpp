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
  // The mode is a question about the window, so it is asked of the window. The
  // panel arithmetic below is a question about the room left over, so it is
  // asked of that -- the ribbon's width is spoken for before anything else has
  // a say in it.
  layout.mode = resolveLayoutMode(window, inputs.previousMode);
  const float usable = std::max(kMinContentWidth, window - kRibbonWidth);

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

  // The title bar spans the window above everything, so every other region
  // starts below it and the panels are that much shorter.
  const float bodyY = kTitleBarHeight;
  const float paneBottom = inputs.windowHeight - kStatusBarHeight;
  const float paneHeight = std::max(0.0f, paneBottom - bodyY);
  const float contentX = kRibbonWidth + sidebar;
  // Floored at zero. On a window too narrow to hold the ribbon, both panels and
  // a page at once, the panels overhang rather than the page being handed a
  // negative width that every hit test downstream would have to defend against.
  const float contentW = std::max(0.0f, inputs.windowWidth - contentX - right);
  const float tabsH = inputs.tabStripVisible ? kTabStripHeight : 0.0f;

  layout.titleBar = {0.0f, 0.0f, inputs.windowWidth, kTitleBarHeight};
  layout.ribbon = {0.0f, bodyY, kRibbonWidth, paneHeight};
  layout.sidebar = {kRibbonWidth, bodyY, sidebar, paneHeight};
  layout.tabs = {contentX, bodyY, contentW, tabsH};
  layout.content = {contentX, bodyY + tabsH, contentW, std::max(0.0f, paneHeight - tabsH)};
  layout.rightPanel = {contentX + contentW, bodyY, right, paneHeight};
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
