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

  // The title bar spans the window above everything, so every other region
  // starts below it and the panels are that much shorter.
  const float bodyY = kTitleBarHeight;
  const float paneBottom = inputs.windowHeight - kStatusBarHeight;
  const float paneHeight = std::max(0.0f, paneBottom - bodyY);
  const float contentX = sidebar + notes;
  const float contentW = inputs.windowWidth - sidebar - notes - right;
  const float tabsH = inputs.tabStripVisible ? kTabStripHeight : 0.0f;

  layout.titleBar = {0.0f, 0.0f, inputs.windowWidth, kTitleBarHeight};
  layout.sidebar = {0.0f, bodyY, sidebar, paneHeight};
  layout.notes = {sidebar, bodyY, notes, paneHeight};
  layout.tabs = {contentX, bodyY, contentW, tabsH};
  layout.content = {contentX, bodyY + tabsH, contentW, std::max(0.0f, paneHeight - tabsH)};
  layout.rightPanel = {contentX + contentW, bodyY, right, paneHeight};
  layout.status = {0.0f, paneBottom, inputs.windowWidth, kStatusBarHeight};
  return layout;
}

}
