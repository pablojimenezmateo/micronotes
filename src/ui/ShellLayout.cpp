#include "ui/ShellLayout.h"

#include "ui/Metrics.h"

#include <algorithm>

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

  const float usable = std::max(inputs.windowWidth, kMinUsableWidth);
  layout.mode = resolveLayoutMode(usable, inputs.previousMode);

  float sidebar = inputs.sidebarWidth;
  float notes = inputs.noteListWidth;
  if(layout.mode == LayoutMode::Compact) {
    sidebar = kCompactSidebarWidth;
    notes = kCompactNoteListWidth;
  }

  sidebar = panelWidth(inputs.sidebarVisible, sidebar, kMinSidebarWidth, kMaxSidebarFraction, usable);
  notes = panelWidth(inputs.noteListVisible, notes, kMinNoteListWidth, kMaxNoteListFraction, usable);
  float right = panelWidth(inputs.rightPanelVisible, inputs.rightPanelWidth, kMinRightPanelWidth,
                           kMaxRightPanelFraction, usable);

  // Whatever the panels asked for, the page keeps kMinContentWidth. The panels
  // give the difference back in the order they are least missed: the right
  // panel is a reference, the note list is reachable from the sidebar, and the
  // sidebar is how you get anywhere at all.
  const auto overflow = [&] {
    return sidebar + notes + right + kMinContentWidth - usable;
  };
  const auto squeeze = [&](float& width, bool visible, float floorWidth) {
    const float over = overflow();
    if(over <= 0.0f || !visible) return;
    width = std::max(floorWidth, width - over);
  };
  squeeze(right, inputs.rightPanelVisible, kRightPanelSqueezeFloor);
  squeeze(notes, inputs.noteListVisible, kNoteListSqueezeFloor);
  squeeze(sidebar, inputs.sidebarVisible, kSidebarSqueezeFloor);

  const float paneHeight = inputs.windowHeight - kStatusBarHeight;
  const float contentX = sidebar + notes;
  const float contentW = inputs.windowWidth - sidebar - notes - right;
  const float tabsH = inputs.tabStripVisible ? kTabStripHeight : 0.0f;
  const float crumbsY = tabsH;
  const float contentY = tabsH + kBreadcrumbHeight;

  layout.sidebar = {0.0f, 0.0f, sidebar, paneHeight};
  layout.notes = {sidebar, 0.0f, notes, paneHeight};
  layout.tabs = {contentX, 0.0f, contentW, tabsH};
  layout.crumbs = {contentX, crumbsY, contentW, kBreadcrumbHeight};
  layout.content = {contentX, contentY, contentW, paneHeight - contentY};
  layout.rightPanel = {contentX + contentW, 0.0f, right, paneHeight};
  layout.status = {0.0f, paneHeight, inputs.windowWidth, kStatusBarHeight};
  return layout;
}

}
