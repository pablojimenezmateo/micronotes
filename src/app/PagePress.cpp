#include "app/PagePress.h"

#include "app/BlockMenus.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Desktop.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Folds.h"
#include "app/LivePage.h"
#include "app/PageChrome.h"
#include "app/PageView.h"
#include "app/RawPane.h"
#include "app/Scroll.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "ui/Metrics.h"

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
// One function because it was two copies -- one for the live surface, one for
// the raw pane and the split view -- identical but for a brace, each spelling
// out the 450 ms threshold and each re-anchoring the drag to the widened
// selection. Two copies of a rule about *time* is the kind that drifts
// silently: nothing about a fourth click behaving differently in one pane than
// the other would fail a test or look wrong in a screenshot.
void beginTextSelection(UiRuntime& ui) {
  ui.textSelect.active = true;
  ui.textSelect.anchor = ui.editor.cursor();
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

  // Grabbing a thumb, and the three panes that have one. The live surface
  // scrolls itself and keeps its own bar; the other two are laid out through
  // `ContentPanes`, so in split view both are on screen and either can be
  // grabbed.
  const auto grab = [&](ScrollDrag which, ui::Rect track, int scroll, int maxScroll) {
    const auto bar = ui::scrollbarGeometry(track, scroll, maxScroll);
    if(!bar || !contains(ui::scrollbarHitRect(bar->thumb), x, y)) return false;
    ui.pointer.scrollDrag = which;
    ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
    return true;
  };

  if(ui.paneMode() == ui::PaneMode::Live) {
    if(!grab(ScrollDrag::Live, ui.livePage.pageRect(), ui.livePage.scroll(), ui.livePage.maxScroll())) {
      return false;
    }
    ui.focus = FocusArea::Editor;
    return true;
  }

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
  // swallow the click. Copying is the one piece of it a read-only page still
  // carries, so it is asked of whichever page is showing the note.
  const bool live = ui.paneMode() == ui::PaneMode::Live;
  const bool overLiveChrome =
    live && (ui.livePage.gutterAt(x, y).has_value() || !ui.livePage.toolbarAt(x, y).empty() ||
             ui.livePage.foldAt(x, y).has_value() || ui.livePage.copyButtonAt(x, y).has_value());
  if(ui.paneMode() != ui::PaneMode::Editor) {
    const PageView& page = live ? ui.livePage : ui.readingPage;
    if(const auto code = codeUnderCopyButton(page, ui.editor.text(), x, y)) {
      ui.status = setClipboardText(*code) ? "Copied code" : "Clipboard unavailable";
      return;
    }
  }
  if(ui.paneMode() != ui::PaneMode::Editor && !overLiveChrome &&
     followLinkAt(ui, x, y)) {
    return;
  }
  if(ui.paneMode() == ui::PaneMode::Live) {
    ui.focus = FocusArea::Editor;
    if(button == SDL_BUTTON_RIGHT) {
      if(const auto index = ui.livePage.blockAt(x, y)) {
        const auto& blocks = ui.livePage.document().blocks();
        if(*index < blocks.size() && !ui.blockSelection.active) {
          ui.editor.moveCursor(blocks[*index].start);
          selectBlockAtCursor(ui);
        }
      }
      openBlockMenu(ui, x, y);
      return;
    }
    if(button != SDL_BUTTON_LEFT) return;

    // The formatting toolbar floats over the page, so it has to win over the
    // text underneath it.
    if(const auto action = ui.livePage.toolbarAt(x, y); !action.empty()) {
      // The toolbar's ids are action names, so the four it shares with the
      // palette and the key chain go through the one chain rather than
      // spelling the markers a second time -- "**" written out twice is two
      // places to get the count of asterisks wrong. The two that are not
      // actions stay here: "strike" has neither a chord nor a palette row,
      // and "turn" needs the point it was clicked at.
      if(action == "strike") wrapEditorSelection(ui, "~~", "~~", "Strikethrough");
      else if(action == "turn") openTurnIntoMenu(ui, x, y);
      else performCommand(ui, std::string(action));
      return;
    }
    // The disclosure control is chrome: it acts and leaves the caret and the
    // selection alone. So is the copy button, handled above for both panes.
    if(const auto fold = ui.livePage.foldAt(x, y)) {
      toggleFoldAt(ui, fold->blockStart);
      return;
    }
    if(const auto hit = ui.livePage.gutterAt(x, y)) {
      const auto& blocks = ui.livePage.document().blocks();
      if(hit->insert) {
        openInsertMenu(ui, blocks[hit->blockIndex].start);
      } else {
        // Grabbing a block outside the selection selects just that one; inside
        // it, the whole selection comes along.
        const auto [from, to] = blockSelectionCarets(ui);
        const bool inside = ui.blockSelection.active && hit->blockStart >= from && hit->blockStart <= to;
        if(!inside) {
          ui.editor.moveCursor(hit->blockStart);
          selectBlockAtCursor(ui);
        }
        const auto [dragFrom, dragTo] = blockSelectionCarets(ui);
        ui.blockDrag.active = true;
        ui.blockDrag.anchor = dragFrom;
        ui.blockDrag.focus = dragTo;
        ui.blockDrag.dropOffset.reset();
      }
      return;
    }
    if((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
      if(ui.blockSelection.active) {
        const auto& blocks = ui.livePage.document().blocks();
        if(const auto index = ui.livePage.blockAt(x, y); index && *index < blocks.size()) {
          ui.blockSelection.focus = blocks[*index].start;
          ui.editor.moveCursor(blocks[*index].start);
        }
      } else {
        ui.editor.moveTo(ui.livePage.offsetAt(x, y), true);
        publishEditorPrimarySelection(ui);
      }
      ui.revealEditorCursor = true;
      return;
    }
    ui.blockSelection.clear();
    // A task checkbox is a control, not text: ticking it must not move the
    // caret or start a selection.
    if(const auto blockStart = ui.livePage.checkboxAt(x, y)) {
      const std::size_t caret = ui.editor.cursor();
      if(applyEdit(ui, doc::toggleTodo(ui.editor.text(), *blockStart, editorBlocks(ui)))) {
        // The flip is a one-byte swap, so every other offset survives it.
        ui.editor.moveCursor(std::min(caret, ui.editor.text().size()));
        ui.revealEditorCursor = false;
        ui.status = "Toggled task";
      }
      return;
    }
    // Clicking into a block the scanner does not model drops it to raw text
    // so it stays editable.
    if(const auto index = ui.livePage.blockAt(x, y)) {
      const auto& blocks = ui.livePage.document().blocks();
      if(*index < blocks.size() && blocks[*index].kind == doc::BlockKind::Complex && !ui.livePage.rawOffset()) {
        ui.livePage.setRawOffset(blocks[*index].start);
        ui.editor.moveCursor(blocks[*index].start);
        ui.editor.clearSelection();
        ui.revealEditorCursor = true;
        ui.status = "Editing block as raw Markdown";
        return;
      }
    }
    ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
    ui.revealEditorCursor = true;
    beginTextSelection(ui);
    return;
  }
  const ContentPanes panes = contentPanes(ui, content);
  if(panes.hasViewer && contains(panes.viewer, x, y)) {
    ui.focus = FocusArea::Viewer;
  } else {
    ui.focus = FocusArea::Editor;
    placeEditorCursor(text, ui, panes.editor, x, y);
    beginTextSelection(ui);
  }
}

}
