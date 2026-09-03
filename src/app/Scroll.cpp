#include "app/Scroll.h"

#include "app/RawPane.h"
#include "app/Sidebar.h"

#include "core/perf/PerformanceCounters.h"

#include "ui/ShellLayout.h"

#include <algorithm>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::contains;

// The editor and the reading pane, side by side in split and each filling the
// content column on its own. Returned together because "which pane is the
// pointer in" is the same question for the wheel as it is for the cursor.
struct ContentPanes {
  Rect editor;
  Rect viewer;
  bool hasEditor = false;
  bool hasViewer = false;
};

ContentPanes contentPanes(const UiRuntime& ui, Rect content) {
  ContentPanes panes {content, content, false, false};
  switch(ui.state.workspace().paneMode()) {
    case ui::PaneMode::Editor:
      panes.hasEditor = true;
      return panes;
    case ui::PaneMode::Viewer:
      panes.hasViewer = true;
      return panes;
    case ui::PaneMode::Live:
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

}

void routeWheel(ui::TextRenderer& text, UiRuntime& ui, float notches, int width, int height) {
  perf::addCounter(perf::CounterId::InputWheelEvents);

  // An open overlay owns the wheel outright.
  if(ui.overlays.active()) {
    ui.overlays.handleWheel(notches);
    return;
  }

  const ShellLayout layout = shellLayout(ui, width, height);

  // Then the panel the pointer is in. The tree can be taller than the window,
  // so the pointer's column decides before the pane mode does.
  if(contains(layout.sidebar, ui.mouseX, ui.mouseY)) {
    ui.sidebarScroll = std::clamp(ui.sidebarScroll + ui.sidebarWheel.take(notches, kSidebarScrollPixelsPerNotch),
                                  0, ui.sidebarMaxScroll);
    return;
  }

  if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
    ui.livePage.setScroll(ui.livePage.scroll() + ui.liveWheel.take(notches, kLiveScrollPixelsPerNotch));
    return;
  }

  const ContentPanes panes = contentPanes(ui, layout.content);
  // With one pane filling the column, focus stands in for the pointer: a wheel
  // delivered while the pointer sits over the status bar still scrolls the note
  // being read.
  const bool single = !(panes.hasEditor && panes.hasViewer);
  const bool wheelViewer = panes.hasViewer &&
    (contains(panes.viewer, ui.mouseX, ui.mouseY) || (single && ui.focus == FocusArea::Viewer));
  const bool wheelEditor = panes.hasEditor &&
    (contains(panes.editor, ui.mouseX, ui.mouseY) || (single && ui.focus == FocusArea::Editor));

  if(wheelViewer) {
    ui.viewerScroll = std::clamp(ui.viewerScroll + ui.viewerWheel.take(notches, kViewerScrollPixelsPerNotch),
                                 0, ui.viewerMaxScroll);
    return;
  }
  if(wheelEditor) {
    ui.editorScroll = std::clamp(ui.editorScroll + ui.editorWheel.take(notches, kEditorScrollLinesPerNotch),
                                 0, editorMaxScroll(text, ui, panes.editor));
    ui.revealEditorCursor = false;
  }
}

}
