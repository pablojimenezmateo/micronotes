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
#include "ui/SearchScope.h"
#include "ui/ShellLayout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace micronotes::app {
namespace {

using micronotes::ui::contains;

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
  if(handleTabStripClick(text, ui, layout.tabs, x, y, button, ctrlHeld)) return;

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
      ui.search.editor.moveCursor(fieldOffsetAtX(text, ui.search, searchTextRect(layout.sidebar, text), x));
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
      Rect editorRect = layout.content;
      bool editorAtPoint = ui.state.workspace().paneMode() == ui::PaneMode::Editor;
      if(ui.state.workspace().paneMode() == ui::PaneMode::Split) {
        editorRect.w = layout.content.w / 2.0f;
        editorAtPoint = contains(editorRect, x, y);
      }
      if(editorAtPoint) {
        ui.focus = FocusArea::Editor;
        placeEditorCursor(text, ui, editorRect, x, y);
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
      ui.scrollDragTarget = ScrollDragTarget::Live;
      ui.scrollDragOffsetY = y - bar->thumb.y;
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
      const auto bar = ui::scrollbarGeometry(writing, ui.editorScroll, maxScroll);
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Editor;
        ui.scrollDragOffsetY = y - bar->thumb.y;
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
        ui.scrollDragTarget = ScrollDragTarget::Viewer;
        ui.scrollDragOffsetY = y - bar->thumb.y;
        ui.focus = FocusArea::Viewer;
        return;
      }
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    if(std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f) {
      ui.resizingSidebar = true;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    for(const auto& region : ui.buttonRegions) {
      if(contains(region.rect, x, y)) {
        performAction(ui, region.action);
        return;
      }
    }
  }

  if(contains(layout.sidebar, x, y)) {
    // The scrollbar first: its thumb overlaps the trailing edge of every row it
    // covers, and a press on a handle has to move the handle rather than select
    // whatever it happens to be lying on.
    if(button == SDL_BUTTON_LEFT) {
      const Rect list = sidebarListRect(layout.sidebar);
      const auto bar = ui::scrollbarGeometry(list, ui.sidebarScroll, ui.sidebarMaxScroll);
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Sidebar;
        ui.scrollDragOffsetY = y - bar->thumb.y;
        return;
      }
    }
    // The search field is part of the sidebar but not part of its row list, so
    // it takes the click before any row arithmetic happens.
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      if(contains(ui.searchScopeToggle, x, y)) {
        ui.searchScope = ui::nextSearchScope(ui.searchScope);
        ui.state.setSearch(ui.search.text(), ui.searchScope);
        ui.status = "Searching " + std::string(ui::searchScopeName(ui.searchScope));
        return;
      }
      // Clicking a text field puts the caret where you clicked. Before, it only
      // moved focus, and the insertion point stayed pinned to the end.
      const Rect fieldRect = searchTextRect(layout.sidebar, text);
      ui.focus = FocusArea::Search;
      const auto offset = fieldOffsetAtX(text, ui.search, fieldRect, x);
      ui.search.editor.moveCursor(offset);
      ui.selectingFieldText = true;
      ui.fieldSelectionAnchor = offset;
      return;
    }

    ui.focus = FocusArea::Folders;
    const auto index = sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y);
    if(!index) {
      if(button == SDL_BUTTON_RIGHT) openFolderMenu(ui, x, y);
      return;
    }
    const SidebarRow row = ui.sidebarRows[*index];
    ui.folderCursor = static_cast<int>(*index);
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
          if(*index < blocks.size() && !ui.blockSelectActive) {
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
        if(action == "bold") wrapEditorSelection(ui, "**", "**", "Bold");
        else if(action == "italic") wrapEditorSelection(ui, "*", "*", "Italic");
        else if(action == "code") wrapEditorSelection(ui, "`", "`", "Code");
        else if(action == "strike") wrapEditorSelection(ui, "~~", "~~", "Strikethrough");
        else if(action == "link") linkEditorSelection(ui);
        else if(action == "turn") openTurnIntoMenu(ui, x, y);
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
          const bool inside = ui.blockSelectActive && hit->blockStart >= from && hit->blockStart <= to;
          if(!inside) {
            ui.editor.moveCursor(hit->blockStart);
            selectBlockAtCursor(ui);
          }
          const auto [dragFrom, dragTo] = blockSelectionCarets(ui);
          ui.draggingBlock = true;
          ui.dragBlockAnchor = dragFrom;
          ui.dragBlockFocus = dragTo;
          ui.blockDropOffset.reset();
        }
        return;
      }
      if((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        if(ui.blockSelectActive) {
          const auto& blocks = ui.livePage.document().blocks();
          if(const auto index = ui.livePage.blockAt(x, y); index && *index < blocks.size()) {
            ui.blockSelectFocus = blocks[*index].start;
            ui.editor.moveCursor(blocks[*index].start);
          }
        } else {
          ui.editor.moveTo(ui.livePage.offsetAt(x, y), true);
          publishEditorPrimarySelection(ui);
        }
        ui.revealEditorCursor = true;
        return;
      }
      ui.clearBlockSelection();
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
      ui.selectingEditorText = true;
      ui.editorSelectionAnchor = ui.editor.cursor();
      const Uint64 now = SDL_GetTicks();
      ui.editorClickCount = now - ui.lastEditorClick < 450 ? ui.editorClickCount + 1 : 1;
      ui.lastEditorClick = now;
      if(ui.editorClickCount == 2) {
        selectWordAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
      } else if(ui.editorClickCount >= 3) {
        selectLineAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
        ui.editorClickCount = 0;
      }
      return;
    }
    if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) ui.focus = FocusArea::Viewer;
    else if(ui.state.workspace().paneMode() == ui::PaneMode::Split && x >= layout.content.x + layout.content.w / 2.0f) ui.focus = FocusArea::Viewer;
    else {
      ui.focus = FocusArea::Editor;
      Rect editorRect = layout.content;
      if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
      placeEditorCursor(text, ui, editorRect, x, y);
      ui.selectingEditorText = true;
      ui.editorSelectionAnchor = ui.editor.cursor();
      const Uint64 now = SDL_GetTicks();
      ui.editorClickCount = now - ui.lastEditorClick < 450 ? ui.editorClickCount + 1 : 1;
      ui.lastEditorClick = now;
      if(ui.editorClickCount == 2) {
        selectWordAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
      }
      else if(ui.editorClickCount >= 3) {
        selectLineAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
        ui.editorClickCount = 0;
      }
    }
  }
}

void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(button == SDL_BUTTON_LEFT) {
    if(ui.draggingBlock) {
      if(ui.blockDropOffset &&
         applyEdit(ui, doc::moveBlocksTo(ui.editor.text(), ui.dragBlockAnchor, ui.dragBlockFocus, *ui.blockDropOffset,
                                           editorBlocks(ui)))) {
        syncBlockSelectionToEdit(ui);
        ui.status = "Moved block";
      }
      ui.draggingBlock = false;
      ui.blockDropOffset.reset();
    }
    if(ui.selectingEditorText) publishEditorPrimarySelection(ui);
    if(ui.selectingFieldText) {
      if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
        SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
      }
    }
    ui.resizingSidebar = false;
    ui.selectingEditorText = false;
    ui.selectingFieldText = false;
    ui.scrollDragTarget = ScrollDragTarget::None;
  }
  if(button != SDL_BUTTON_LEFT || (!ui.draggingNote && !ui.draggingFolder)) return;
  const ShellLayout layout = shellLayout(ui, width, height);
  const auto index = sidebarRowAt(ui, layout.sidebar, x, y);
  if(index && ui.sidebarRows[*index].kind == SidebarRow::Kind::Tree) {
    // A note row stands for the folder holding it, so dropping between two
    // notes does the obvious thing rather than nothing.
    const auto target = ui.sidebarRows[*index].tree.folder;
    if(ui.draggingNote) {
      selectNoteById(ui, ui.draggingNoteId);
      if(ui.state.moveSelectedNoteToFolder(target)) {
        ui.tree.reveal(target);
        ui.status = "Moved note to " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
      } else {
        ui.status = "Move note failed";
      }
    } else if(ui.state.moveFolderInto(ui.draggingFolderPath, target)) {
      ui.tree.reveal(target / ui.draggingFolderPath.filename());
      ui.status = "Moved notebook into " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
    } else if(target != ui.draggingFolderPath.parent_path() && target != ui.draggingFolderPath) {
      ui.status = "Cannot move a notebook into itself";
    }
  }
  ui.draggingNote = false;
  ui.draggingNoteId.clear();
  ui.draggingFolder = false;
  ui.draggingFolderPath.clear();
  ui.sidebarDropRow.reset();
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
  if(ui.openMenu != ui::MenuId::None) {
    // Sliding along the bar with a menu open switches menus without a click,
    // which is the whole of what makes a menu bar feel like one rather than
    // like seven buttons.
    const ShellLayout layout = shellLayout(ui, width, height);
    (void)handleMenuBarMotion(text, ui, layout.menuBar,
                              {0, 0, static_cast<float>(width), static_cast<float>(height)}, x, y);
    return;
  }
  if(ui.draggingBlock) {
    ui.blockDropOffset = ui.livePage.dropOffsetAt(y);
    return;
  }
  if(ui.selectingEditorText) {
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      ui.editor.selectRange(ui.editorSelectionAnchor, ui.livePage.offsetAt(x, y));
    } else {
      const ShellLayout layout = shellLayout(ui, width, height);
      Rect editorRect = layout.content;
      if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
      ui.editor.selectRange(ui.editorSelectionAnchor, editorIndexAtPoint(text, ui, editorRect, x, y));
    }
    ui.revealEditorCursor = true;
    return;
  }
  if(ui.scrollDragTarget != ScrollDragTarget::None) {
    const ShellLayout layout = shellLayout(ui, width, height);
    switch(ui.scrollDragTarget) {
      case ScrollDragTarget::Live:
        ui.livePage.setScroll(
          scrollFromThumbY(ui.livePage.pageRect(), y, ui.scrollDragOffsetY, ui.livePage.maxScroll()));
        break;
      case ScrollDragTarget::Sidebar:
        ui.sidebarScroll = scrollFromThumbY(sidebarListRect(layout.sidebar), y, ui.scrollDragOffsetY,
                                            ui.sidebarMaxScroll);
        break;
      case ScrollDragTarget::Editor: {
        Rect editorRect = layout.content;
        if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
        ui.editorScroll = scrollFromThumbY(editorWritingRect(editorRect), y, ui.scrollDragOffsetY,
                                           editorMaxScroll(text, ui, editorRect));
        ui.revealEditorCursor = false;
        break;
      }
      case ScrollDragTarget::Viewer: {
        const Rect page = ui::pageRectIn(contentPanes(ui, layout.content).viewer);
        ui.readingPage.setScroll(
          scrollFromThumbY(page, y, ui.scrollDragOffsetY, ui.readingPage.maxScroll()));
        break;
      }
      case ScrollDragTarget::None: break;
    }
    return;
  }
  if(ui.draggingNote || ui.draggingFolder) {
    const ShellLayout layout = shellLayout(ui, width, height);
    const auto row = sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y);
    ui.sidebarDropRow = row && ui.sidebarRows[*row].kind == SidebarRow::Kind::Tree
                          ? row
                          : std::optional<std::size_t> {};
    return;
  }
  if(ui.resizingSidebar) {
    ui.state.workspace().sidebarWidth =
      std::clamp(x, ui::kMinSidebarWidth, std::max(ui::kMinSidebarWidth, static_cast<float>(width) - 520.0f));
  }
}

}
