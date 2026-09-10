#include "app/Scroll.h"
#include "app/Layout.h"

#include "app/RawPane.h"
#include "app/SettingsPane.h"
#include "app/Sidebar.h"

#include "core/perf/PerformanceCounters.h"

#include "ui/ShellLayout.h"

#include <algorithm>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::contains;

}

ContentPanes contentPanes(const UiRuntime& ui, Rect content) {
  ContentPanes panes {content, content, false, false};
  switch(ui.paneMode()) {
    case ui::PaneMode::Editor:
      panes.hasEditor = true;
      return panes;
    case ui::PaneMode::Viewer:
      panes.hasViewer = true;
      return panes;
    case ui::PaneMode::Split:
      break;
  }
  panes.hasEditor = true;
  panes.hasViewer = true;
  panes.editor.w = content.w / 2.0f;
  panes.viewer = {content.x + panes.editor.w, content.y, content.w - panes.editor.w, content.h};
  return panes;
}

void routeWheel(UiRuntime& ui, float notches, int width, int height) {
  perf::addCounter(perf::CounterId::InputWheelEvents);

  // An open overlay owns the wheel outright, and so does the Settings card:
  // both are modal, and a list scrolling behind a modal is a list nobody asked
  // to scroll.
  if(ui.overlays.active()) {
    ui.overlays.handleWheel(notches);
    return;
  }
  if(handleSettingsWheel(ui, notches)) return;

  const ShellLayout layout = shellLayout(ui, width, height);

  // Then whichever panel the pointer is in. The tree and the outline can both
  // be taller than the window, so the pointer's column decides before the pane
  // mode does.
  if(contains(layout.sidebar, ui.pointer.x, ui.pointer.y)) {
    ui.sidebar.list.wheel(notches, kSidebarScrollPixelsPerNotch);
    return;
  }
  if(contains(layout.rightPanel, ui.pointer.x, ui.pointer.y)) {
    ui.rightPanel.list.wheel(notches, kRightPanelScrollPixelsPerNotch);
    return;
  }

  const ContentPanes panes = contentPanes(ui, layout.content);
  // With one pane filling the column, focus stands in for the pointer: a wheel
  // delivered while the pointer sits over the status bar still scrolls the note
  // being read.
  const bool single = !(panes.hasEditor && panes.hasViewer);
  const bool wheelViewer = panes.hasViewer &&
    (contains(panes.viewer, ui.pointer.x, ui.pointer.y) || (single && ui.focus == FocusArea::Viewer));
  const bool wheelEditor = panes.hasEditor &&
    (contains(panes.editor, ui.pointer.x, ui.pointer.y) || (single && ui.focus == FocusArea::Editor));

  if(wheelViewer) {
    // The page owns its scroll and clamps it to what it last laid out, so the
    // wheel adds and the page decides -- rather than the runtime keeping a
    // second copy of both numbers and clamping them against each other.
    ui.readingPage.wheel(notches, kViewerScrollPixelsPerNotch);
    return;
  }
  if(wheelEditor) {
    ui.raw.list.wheel(notches, kEditorScrollLinesPerNotch);
    ui.revealEditorCursor = false;
  }
}

}
