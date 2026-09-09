#include "app/Cursor.h"

#include "app/Layout.h"

#include "app/Breadcrumb.h"
#include "app/MenuBar.h"
#include "app/PageChrome.h"
#include "app/RawPane.h"
#include "app/RightPanel.h"
#include "app/Scroll.h"
#include "app/SettingsPane.h"
#include "app/Shell.h"
#include "app/Sidebar.h"
#include "app/SidebarModel.h"
#include "app/TabStrip.h"

#include "ui/Scrollbar.h"
#include "ui/TextRenderer.h"
#include "ui/ShellLayout.h"

#include <cmath>

namespace micronotes::app {

using ui::contains;
using ui::Rect;
using ui::ShellLayout;
using ui::TextRenderer;

namespace {

// How an overlay's answer about the pointer reads as a system cursor.
//
// The shell used to answer `Pointer` for the whole window whenever any overlay
// was open, so the field in a rename box, a tag editor or the command palette
// never showed a text cursor -- and those are the fields a reader is most
// likely to be typing into.
CursorKind cursorForOverlay(ui::OverlayCursor over) {
  switch(over) {
    case ui::OverlayCursor::Text: return CursorKind::Text;
    case ui::OverlayCursor::Pointer: return CursorKind::Pointer;
    // Over the panel's own ground, or outside it: a click there does nothing
    // and a click there dismisses, and neither is a control to point at.
    case ui::OverlayCursor::Panel:
    case ui::OverlayCursor::Outside: break;
  }
  return CursorKind::Default;
}

// A hidden panel has no edge to grab: its width is zero, so its right edge sits
// on top of the next panel's left one and dragging there would resize a panel
// nobody can see.
bool isResizeGutter(const ShellLayout& layout, float x, float y) {
  if(y < layout.sidebar.y || y > layout.sidebar.y + layout.sidebar.h) return false;
  const auto nearEdge = [&](const Rect& panel) {
    return !ui::empty(panel) && std::abs(x - (panel.x + panel.w)) <= ui::kResizeGutterInflate + 1.0f;
  };
  return nearEdge(layout.sidebar) ||
         (!ui::empty(layout.rightPanel) &&
          std::abs(x - layout.rightPanel.x) <= ui::kResizeGutterInflate + 1.0f);
}


bool scrollbarHit(Rect viewport, int scroll, int maxScroll, float x, float y) {
  const auto geometry = ui::scrollbarGeometry(viewport, scroll, maxScroll);
  return geometry && contains(ui::scrollbarHitRect(geometry->thumb), x, y);
}

}

CursorKind classifyCursor(TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(ui.sidebar.resizing) return CursorKind::ResizeHorizontal;
  if(ui.pointer.scrollDrag != ScrollDrag::None) return CursorKind::ResizeVertical;

  const float x = ui.pointer.x;
  const float y = ui.pointer.y;
  const ShellLayout layout = shellLayout(ui, width, height);
  if(isResizeGutter(layout, x, y)) return CursorKind::ResizeHorizontal;
  if(menuBarHasControlAt(text, ui, layout.menuBar, x, y)) return CursorKind::Pointer;
  if(breadcrumbHasControlAt(ui, layout.breadcrumb, x, y)) return CursorKind::Pointer;
  // The two bands that answered a click and said nothing about it: a tab, its
  // close cross and the overflow chevrons, and every row and view tab in the
  // right panel. Both are lists of controls, and a control the pointer does not
  // change shape over is one the reader has to discover by clicking.
  // A hidden band is an empty rect, and both of these test theirs first, so
  // there is no visibility flag to keep in step with here.
  if(tabStripHasControlAt(ui, x, y)) return CursorKind::Pointer;
  if(rightPanelHasControlAt(ui, text, layout.rightPanel, x, y)) return CursorKind::Pointer;

  // Which part of the overlay, rather than one answer for the whole window.
  if(ui.overlays.active()) return cursorForOverlay(ui.overlays.cursorAt(x, y));
  // The card covers the panes below, so the pointer resting on one of its rows
  // is not resting on the note underneath.
  if(ui.settings.visible) {
    return contains(ui.settings.filter, x, y) ? CursorKind::Text : CursorKind::Pointer;
  }

  if(contains(layout.sidebar, x, y)) {
    if(scrollbarHit(sidebarListRect(layout.sidebar), ui.sidebar.list.scroll(), ui.sidebar.list.maxScroll(), x, y)) {
      return CursorKind::Pointer;
    }
    const Rect search = searchBoxRect(layout.sidebar);
    if(contains(search, x, y)) {
      return contains(ui.sidebar.scopeToggle, x, y) ? CursorKind::Pointer : CursorKind::Text;
    }
    if(sidebarRowAt(ui, x, y)) return CursorKind::Pointer;
    return CursorKind::Default;
  }

  if(!contains(layout.content, x, y)) return CursorKind::Default;

  if(ui.paneMode() == ui::PaneMode::Live) {
    if(scrollbarHit(ui.livePage.pageRect(), ui.livePage.scroll(), ui.livePage.maxScroll(), x, y)) return CursorKind::Pointer;
    if(!ui.livePage.linkAt(x, y).empty()) return CursorKind::Pointer;
    if(ui.livePage.gutterAt(x, y) || !ui.livePage.toolbarAt(x, y).empty()) return CursorKind::Pointer;
    if(ui.livePage.foldAt(x, y) || ui.livePage.copyButtonAt(x, y)) return CursorKind::Pointer;
    if(ui.livePage.checkboxAt(x, y)) return CursorKind::Pointer;
    return contains(ui.livePage.pageRect(), x, y) ? CursorKind::Text : CursorKind::Default;
  }

  const ContentPanes panes = contentPanes(ui, layout.content);
  const Rect editorRect = panes.editor;
  const Rect viewerRect = panes.viewer;
  const bool hasEditor = panes.hasEditor;
  const bool hasViewer = panes.hasViewer;

  if(hasEditor && contains(editorRect, x, y)) {
    const Rect writing = editorWritingRect(editorRect);
    if(scrollbarHit(writing, ui.raw.list.scroll(), ui.raw.list.maxScroll(), x, y)) {
      return CursorKind::Pointer;
    }
    if(contains(writing, x, y)) return CursorKind::Text;
  }

  if(hasViewer && contains(viewerRect, x, y)) {
    const Rect page = ui::pageRectIn(viewerRect);
    if(scrollbarHit(page, ui.readingPage.scroll(), ui.readingPage.maxScroll(), x, y)) {
      return CursorKind::Pointer;
    }
    if(ui.readingPage.copyButtonAt(x, y)) return CursorKind::Pointer;
  }

  // Every link the frame drew, wherever it drew it. Asked after the panes so a
  // scrollbar lying over one still wins, and outside them so that a link in the
  // page header, or in whichever pane of a split the pointer is in, is a link.
  //
  // Through `linkAt`, which is also what the click and the middle-click ask, so
  // the shape the pointer takes cannot promise a click that lands elsewhere.
  if(linkAt(ui, x, y)) return CursorKind::Pointer;

  return CursorKind::Default;
}

}
