#include "app/PagePress.h"

#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Desktop.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/PageChrome.h"
#include "app/PageView.h"
#include "app/RawPane.h"
#include "app/Scroll.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "ui/Metrics.h"
#include "ui/Scrollbar.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace micronotes::app {
namespace {

using micronotes::ui::contains;

// A press into the note's text, once the caret has been placed: it starts a
// selection, and a second or third press in quick succession widens it to the
// word or the line.
//
// One function because it was two copies -- one per surface -- identical but
// for a brace, each spelling out the 450 ms threshold and each re-anchoring the
// drag to the widened selection. Two copies of a rule about *time* is the kind
// that drifts silently: nothing about a fourth click behaving differently in
// one pane than the other would fail a test or look wrong in a screenshot.
void beginTextSelection(UiRuntime& ui, SelectSurface surface) {
  ui.textSelect.active = true;
  ui.textSelect.anchor = ui.editor.cursor();
  ui.textSelect.surface = surface;
  const int clicks = ui.editorClicks.extend(SDL_GetTicks());
  if(clicks < 2) return;
  // Re-anchored to the widened selection, so dragging on from a double click
  // extends from the word rather than from where the pointer happened to land.
  if(clicks == 2) selectWordAtCursor(ui);
  else {
    selectLineAtCursor(ui);
    ui.editorClicks.reset();
  }
  ui.textSelect.anchor = ui.editor.selectionStart();
  publishEditorPrimarySelection(ui);
}

}

bool pressPaneScrollbar(UiRuntime& ui, Rect content, float x, float y, Uint8 button) {
  if(button != SDL_BUTTON_LEFT || !contains(content, x, y)) return false;

  // Grabbing a thumb, and the two panes that have one. Both are laid out
  // through `ContentPanes`, so in split view both are on screen and either can
  // be grabbed.
  const auto grab = [&](ScrollDrag which, ui::Rect track, int scroll, int maxScroll) {
    const auto bar = ui::scrollbarGeometry(track, scroll, maxScroll);
    if(!bar || !contains(ui::scrollbarHitRect(bar->thumb), x, y)) return false;
    ui.pointer.scrollDrag = which;
    ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
    return true;
  };

  const ContentPanes panes = contentPanes(ui, content);
  if(panes.hasEditor) {
    const Rect editorRect = panes.editor;
    if(grab(ScrollDrag::RawPane, editorWritingRect(editorRect), ui.raw.list.scroll(),
            ui.raw.list.maxScroll())) {
      ui.focus = FocusArea::Editor;
      // A bar dragged deliberately must not be undone by the caret it left
      // behind on the row it came from.
      ui.revealEditorCursor = false;
      return true;
    }
  }
  if(panes.hasViewer) {
    if(grab(ScrollDrag::Reading, ui::pageRectIn(panes.viewer), ui.readingPage.scroll(),
            ui.readingPage.maxScroll())) {
      ui.focus = FocusArea::Viewer;
      return true;
    }
  }
  return false;
}

void pressPage(TextRenderer& text, UiRuntime& ui, Rect content, float x, float y, Uint8 button) {
  // A page's own chrome sits above its text, so a link underneath it must not
  // swallow the click. The copy button is the whole of that chrome, and it is
  // asked before the link so a button drawn over one still wins.
  if(ui.paneMode() != ui::PaneMode::Editor) {
    if(const auto code = codeUnderCopyButton(ui.readingPage, ui.editor.text(), x, y)) {
      ui.status = setClipboardText(*code) ? "Copied code" : "Clipboard unavailable";
      return;
    }
    if(followLinkAt(ui, x, y)) return;
  }
  const ContentPanes panes = contentPanes(ui, content);
  if(panes.hasViewer && contains(panes.viewer, x, y)) {
    ui.focus = FocusArea::Viewer;
    if(button != SDL_BUTTON_LEFT) return;
    // A task is a control, not text: ticking it must not move the caret or
    // start a selection. The pane drew its checkboxes and answered
    // `checkboxAt` for them long before a click on one did anything at all --
    // a tick-box you cannot tick is a picture of a task.
    if(const auto blockStart = ui.readingPage.checkboxAt(x, y)) {
      const std::size_t caret = ui.editor.cursor();
      if(applyEdit(ui, doc::toggleTodo(ui.editor.text(), *blockStart, editorBlocks(ui)))) {
        // The flip is a one-byte swap, so every other offset survives it.
        ui.editor.moveCursor(std::min(caret, ui.editor.text().size()));
        ui.revealEditorCursor = false;
        ui.status = "Toggled task";
      }
      return;
    }
    // The pane is read-only, not untouchable: the selection it makes is the
    // buffer's, so the note it is reading is the note that gets copied.
    ui.editor.moveCursor(ui.readingPage.offsetAt(x, y));
    beginTextSelection(ui, SelectSurface::ReadingPage);
  } else {
    ui.focus = FocusArea::Editor;
    placeEditorCursor(text, ui, panes.editor, x, y);
    beginTextSelection(ui, SelectSurface::RawPane);
  }
}

}
