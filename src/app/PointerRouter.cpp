#include "app/PointerRouter.h"

#include "app/BlockMenus.h"
#include "app/Breadcrumb.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/ContextMenus.h"
#include "app/Desktop.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Fields.h"
#include "app/Folds.h"
#include "app/LivePage.h"
#include "app/MenuBar.h"
#include "app/Notes.h"
#include "app/OverlayRouter.h"
#include "app/PageChrome.h"
#include "app/PageView.h"
#include "app/RawPane.h"
#include "app/RightPanel.h"
#include "app/Scroll.h"
#include "app/Shell.h"
#include "app/Sidebar.h"
#include "app/SidebarModel.h"
#include "app/TabStrip.h"
#include "app/WikiLinks.h"
#include "app/WindowChrome.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "ui/Metrics.h"
#include "library/SearchScope.h"
#include "ui/ShellLayout.h"

#include <algorithm>
#include <cmath>
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

void handleMouse(TextRenderer& text, UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleClick(x, y, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  const ShellLayout layout = shellLayout(ui, width, height);

  // The menu bar first, and its popup before that: the popup is drawn over
  // every panel, so it has to be clicked before them too. A press that misses
  // both only dismisses an open menu -- it is not swallowed, because clicking a
  // tree row with the File menu open should select that row.
  const Rect windowRect {0, 0, static_cast<float>(width), static_cast<float>(height)};
  if(button == SDL_BUTTON_LEFT) {
    const MenuBarClick menu = handleMenuBarClick(text, ui, layout.menuBar, windowRect, x, y);
    if(menu.action) {
      if(const auto* spec = ui::findAction(*menu.action)) performCommand(ui, std::string(spec->name));
    }
    if(menu.handled) return;
  }

  const bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
  if(handleTabStripClick(ui, x, y, button, ctrlHeld)) return;

  // The right panel owns everything inside it, including its own background:
  // without that, a click between two outline rows would fall through to the
  // page and move the caret somewhere the reader never pointed at.
  if((button == SDL_BUTTON_LEFT || button == SDL_BUTTON_MIDDLE) && !ui::empty(layout.rightPanel) &&
     handleRightPanelClick(ui, text, layout.rightPanel, x, y)) {
    return;
  }

  // A middle click pastes the primary selection into anything that takes text,
  // which is the X11 convention this app deliberately follows. A sidebar row
  // and a link take none -- and the block below returned for *every* middle
  // click, so one on a note either did nothing or pasted into whatever field
  // still had focus, and could not mean "open in a new tab" anywhere.
  const bool middleOpensATab = button == SDL_BUTTON_MIDDLE &&
    (contains(sidebarListRect(layout.sidebar), x, y) || pointOnLink(ui, x, y));
  if(button == SDL_BUTTON_MIDDLE && !middleOpensATab) {
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      ui.focus = FocusArea::Search;
      // Middle-click pastes at the point pressed, like every other X11 text
      // field, rather than always at the end of the string.
      ui.fields.search.editor.moveCursor(fieldOffsetAtX(text, ui.fields.search, searchTextRect(layout.sidebar, text), x));
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y) && ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      ui.focus = FocusArea::Editor;
      ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
      ui.revealEditorCursor = true;
      ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y)) {
      const ContentPanes panes = contentPanes(ui, layout.content);
      if(panes.hasEditor && contains(panes.editor, x, y)) {
        ui.focus = FocusArea::Editor;
        placeEditorCursor(text, ui, panes.editor, x, y);
        ui.revealEditorCursor = true;
        ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
        return;
      }
    }
    if(auto* field = focusedField(ui)) {
      // The search box is the one field drawn in a pane, so a middle click
      // inside it drops the caret where it landed before pasting.
      if(ui.focus == FocusArea::Search) {
        const Rect fieldRect = searchTextRect(layout.sidebar, text);
        if(contains(fieldRect, x, y)) {
          field->editor.moveCursor(fieldOffsetAtX(text, *field, fieldRect, x));
        }
      }
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
    }
    return;
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.workspace().paneMode() == ui::PaneMode::Live) {
    const int maxScroll = ui.livePage.maxScroll();
    const auto bar = ui::scrollbarGeometry(ui.livePage.pageRect(), ui.livePage.scroll(), maxScroll);
    if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
      ui.pointer.scrollDrag = ScrollDrag::Live;
      ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
      ui.focus = FocusArea::Editor;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.workspace().paneMode() != ui::PaneMode::Live) {
    const ContentPanes panes = contentPanes(ui, layout.content);
    if(panes.hasEditor) {
      const Rect editorRect = panes.editor;
      const Rect writing = editorWritingRect(editorRect);
      const int maxScroll = editorMaxScroll(text, ui, editorRect);
      const auto bar = ui::scrollbarGeometry(writing, ui.raw.scroll, maxScroll);
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.pointer.scrollDrag = ScrollDrag::RawPane;
        ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
        ui.focus = FocusArea::Editor;
        ui.revealEditorCursor = false;
        return;
      }
    }
    if(panes.hasViewer) {
      const Rect page = ui::pageRectIn(panes.viewer);
      const int maxScroll = ui.readingPage.maxScroll();
      const auto bar = ui::scrollbarGeometry(page, ui.readingPage.scroll(), maxScroll);
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.pointer.scrollDrag = ScrollDrag::Reading;
        ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
        ui.focus = FocusArea::Viewer;
        return;
      }
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    if(std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f) {
      ui.sidebar.resizing = true;
      return;
    }
  }

  if(contains(layout.sidebar, x, y)) {
    // The scrollbar first: its thumb overlaps the trailing edge of every row it
    // covers, and a press on a handle has to move the handle rather than select
    // whatever it happens to be lying on.
    if(button == SDL_BUTTON_LEFT) {
      const Rect list = sidebarListRect(layout.sidebar);
      const auto bar = ui::scrollbarGeometry(list, ui.sidebar.scroll, ui.sidebar.maxScroll);
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.pointer.scrollDrag = ScrollDrag::Sidebar;
        ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
        return;
      }
    }
    // The search field is part of the sidebar but not part of its row list, so
    // it takes the click before any row arithmetic happens.
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      if(contains(ui.sidebar.scopeToggle, x, y)) {
        ui.fields.searchScope = library::nextSearchScope(ui.fields.searchScope);
        ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
        ui.status = "Searching " + std::string(library::searchScopeName(ui.fields.searchScope));
        return;
      }
      // Clicking a text field puts the caret where you clicked. Before, it only
      // moved focus, and the insertion point stayed pinned to the end.
      const Rect fieldRect = searchTextRect(layout.sidebar, text);
      ui.focus = FocusArea::Search;
      const auto offset = fieldOffsetAtX(text, ui.fields.search, fieldRect, x);
      ui.fields.search.editor.moveCursor(offset);
      ui.fieldSelect.active = true;
      ui.fieldSelect.anchor = offset;
      return;
    }

    ui.focus = FocusArea::Folders;
    const auto index = sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y);
    if(!index) {
      if(button == SDL_BUTTON_RIGHT) openFolderMenu(ui, x, y);
      return;
    }
    const SidebarRow row = ui.sidebar.rows[*index];
    ui.sidebar.cursor = static_cast<int>(*index);
    // What a press on a row means lives with the rows. See `pressSidebarRow`.
    pressSidebarRow(ui, row, x, y, button);
    return;
  }
  if(contains(layout.menuBar, x, y)) {
    if(pressWindowButton(ui, x, y, button)) return;
    return;
  }
  if(handleBreadcrumbClick(ui, layout.breadcrumb, x, y)) return;
  if(contains(layout.content, x, y)) {
    // A page's own chrome sits above its text, so a link underneath it must not
    // swallow the click. Copying is the one piece of it a read-only page still
    // carries, so it is asked of whichever page is showing the note.
    const bool live = ui.state.workspace().paneMode() == ui::PaneMode::Live;
    const bool overLiveChrome =
      live && (ui.livePage.gutterAt(x, y).has_value() || !ui.livePage.toolbarAt(x, y).empty() ||
               ui.livePage.foldAt(x, y).has_value() || ui.livePage.copyButtonAt(x, y).has_value());
    if(ui.state.workspace().paneMode() != ui::PaneMode::Editor) {
      const PageView& page = live ? ui.livePage : ui.readingPage;
      if(const auto code = codeUnderCopyButton(page, ui.editor.text(), x, y)) {
        ui.status = setClipboardText(*code) ? "Copied code" : "Clipboard unavailable";
        return;
      }
    }
    if(ui.state.workspace().paneMode() != ui::PaneMode::Editor && !overLiveChrome &&
       followLinkAt(ui, x, y)) {
      return;
    }
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
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
    const ContentPanes panes = contentPanes(ui, layout.content);
    if(panes.hasViewer && contains(panes.viewer, x, y)) {
      ui.focus = FocusArea::Viewer;
    } else {
      ui.focus = FocusArea::Editor;
      placeEditorCursor(text, ui, panes.editor, x, y);
      beginTextSelection(ui);
    }
  }
}

void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(button == SDL_BUTTON_LEFT) {
    if(ui.blockDrag.active) {
      if(ui.blockDrag.dropOffset &&
         applyEdit(ui, doc::moveBlocksTo(ui.editor.text(), ui.blockDrag.anchor, ui.blockDrag.focus, *ui.blockDrag.dropOffset,
                                           editorBlocks(ui)))) {
        syncBlockSelectionToEdit(ui);
        ui.status = "Moved block";
      }
      ui.blockDrag.active = false;
      ui.blockDrag.dropOffset.reset();
    }
    if(ui.textSelect.active) publishEditorPrimarySelection(ui);
    if(ui.fieldSelect.active) {
      if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
        SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
      }
    }
    ui.sidebar.resizing = false;
    ui.textSelect.active = false;
    ui.fieldSelect.active = false;
    ui.pointer.scrollDrag = ScrollDrag::None;
  }
  if(button != SDL_BUTTON_LEFT || !ui.sidebar.drag.active()) return;
  const ShellLayout layout = shellLayout(ui, width, height);
  const auto index = sidebarRowAt(ui, layout.sidebar, x, y);
  if(index && ui.sidebar.rows[*index].kind == SidebarRow::Kind::Tree) {
    // A note row stands for the folder holding it, so dropping between two
    // notes does the obvious thing rather than nothing.
    const auto target = ui.sidebar.rows[*index].tree.folder;
    if(ui.sidebar.drag.note) {
      selectNoteById(ui, ui.sidebar.drag.noteId);
      if(ui.state.moveSelectedNoteToFolder(target)) {
        ui.sidebar.tree.reveal(target);
        ui.status = "Moved note to " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
      } else {
        ui.status = "Move note failed";
      }
    } else if(ui.state.moveFolderInto(ui.sidebar.drag.folderPath, target)) {
      ui.sidebar.tree.reveal(target / ui.sidebar.drag.folderPath.filename());
      ui.status = "Moved notebook into " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
    } else if(target != ui.sidebar.drag.folderPath.parent_path() && target != ui.sidebar.drag.folderPath) {
      ui.status = "Cannot move a notebook into itself";
    }
  }
  ui.sidebar.drag.clear();
}


// Answers to whichever gesture is in flight. The order is the press path's in
// reverse: whatever grabbed the pointer keeps it until the button comes up, so
// a drag that started on a scrollbar thumb is not stolen by the text it passes
// over.
//
// This was the body of the event loop's motion arm, which is why the drags had
// no test: `blockDropOffset`, the four scrollbar thumbs and the sidebar's own
// edge were only ever reachable from a live SDL event.
void handleMouseMotion(TextRenderer& text, UiRuntime& ui, float x, float y, int width, int height) {
  if(ui.overlays.active()) {
    ui.overlays.handleMotion(x, y);
    return;
  }
  if(ui.chrome.openMenu != ui::MenuId::None) {
    // Sliding along the bar with a menu open switches menus without a click,
    // which is the whole of what makes a menu bar feel like one rather than
    // like seven buttons.
    const ShellLayout layout = shellLayout(ui, width, height);
    (void)handleMenuBarMotion(text, ui, layout.menuBar,
                              {0, 0, static_cast<float>(width), static_cast<float>(height)}, x, y);
    return;
  }
  if(ui.blockDrag.active) {
    ui.blockDrag.dropOffset = ui.livePage.dropOffsetAt(y);
    return;
  }
  if(ui.textSelect.active) {
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      ui.editor.selectRange(ui.textSelect.anchor, ui.livePage.offsetAt(x, y));
    } else {
      const ShellLayout layout = shellLayout(ui, width, height);
      const Rect editorRect = contentPanes(ui, layout.content).editor;
      ui.editor.selectRange(ui.textSelect.anchor, editorIndexAtPoint(text, ui, editorRect, x, y));
    }
    ui.revealEditorCursor = true;
    return;
  }
  if(ui.pointer.scrollDrag != ScrollDrag::None) {
    const ShellLayout layout = shellLayout(ui, width, height);
    switch(ui.pointer.scrollDrag) {
      case ScrollDrag::Live:
        ui.livePage.setScroll(
          scrollFromThumbY(ui.livePage.pageRect(), y, ui.pointer.scrollDragOffsetY, ui.livePage.maxScroll()));
        break;
      case ScrollDrag::Sidebar:
        ui.sidebar.scroll = scrollFromThumbY(sidebarListRect(layout.sidebar), y, ui.pointer.scrollDragOffsetY,
                                            ui.sidebar.maxScroll);
        break;
      case ScrollDrag::RawPane: {
        const Rect editorRect = contentPanes(ui, layout.content).editor;
        ui.raw.scroll = scrollFromThumbY(editorWritingRect(editorRect), y, ui.pointer.scrollDragOffsetY,
                                           editorMaxScroll(text, ui, editorRect));
        ui.revealEditorCursor = false;
        break;
      }
      case ScrollDrag::Reading: {
        const Rect page = ui::pageRectIn(contentPanes(ui, layout.content).viewer);
        ui.readingPage.setScroll(
          scrollFromThumbY(page, y, ui.pointer.scrollDragOffsetY, ui.readingPage.maxScroll()));
        break;
      }
      case ScrollDrag::None: break;
    }
    return;
  }
  if(ui.sidebar.drag.active()) {
    const ShellLayout layout = shellLayout(ui, width, height);
    const auto row = sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y);
    ui.sidebar.drag.dropRow = row && ui.sidebar.rows[*row].kind == SidebarRow::Kind::Tree
                          ? row
                          : std::optional<std::size_t> {};
    return;
  }
  if(ui.sidebar.resizing) {
    ui.state.workspace().sidebarWidth =
      std::clamp(x, ui::kMinSidebarWidth, std::max(ui::kMinSidebarWidth, static_cast<float>(width) - 520.0f));
  }
}

}
