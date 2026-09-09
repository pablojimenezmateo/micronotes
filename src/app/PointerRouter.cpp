#include "app/PointerRouter.h"
#include "app/Layout.h"

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
#include "app/PagePress.h"
#include "app/PageView.h"
#include "app/RawPane.h"
#include "app/RightPanel.h"
#include "app/Scroll.h"
#include "app/SettingsPane.h"
#include "app/Shell.h"
#include "library/Library.h"
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
#include "ui/Scrollbar.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace micronotes::app {
namespace {

using micronotes::ui::contains;

// Where a middle click pastes.
//
// X11 has a second selection and a middle click pastes it into whatever takes
// text, which is the convention this app deliberately follows -- so the answer
// depends on what the pointer is over, and this is the routing rather than the
// paste. `app/Clipboard.h` does the pasting.
//
// The two things it must *not* claim are a sidebar row and a link, because
// neither takes text and a middle click on one means "open in a new tab". The
// block this came from returned for every middle click, so one on a note either
// did nothing or pasted into whichever field still had focus.
bool pastePrimaryWherePointed(TextRenderer& text, UiRuntime& ui, const ShellLayout& layout,
                              float x, float y, Uint8 button) {
  if(button != SDL_BUTTON_MIDDLE) return false;
  if(contains(sidebarListRect(layout.sidebar), x, y) || pointOnLink(ui, x, y)) return false;
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      ui.focus = FocusArea::Search;
      // Middle-click pastes at the point pressed, like every other X11 text
      // field, rather than always at the end of the string.
      ui.fields.search.editor.moveCursor(fieldOffsetAtX(text, ui.fields.search, searchTextRect(layout.sidebar, text), x));
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
      return true;
    }
    if(contains(layout.content, x, y) && ui.paneMode() == ui::PaneMode::Live) {
      ui.focus = FocusArea::Editor;
      ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
      ui.revealEditorCursor = true;
      ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
      return true;
    }
    if(contains(layout.content, x, y)) {
      const ContentPanes panes = contentPanes(ui, layout.content);
      if(panes.hasEditor && contains(panes.editor, x, y)) {
        ui.focus = FocusArea::Editor;
        placeEditorCursor(text, ui, panes.editor, x, y);
        ui.revealEditorCursor = true;
        ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
        return true;
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
    return true;
  return true;
}

}

void handleMouse(TextRenderer& text, UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleClick(x, y, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  // The Settings card is modal: while it is open it owns every press, either as
  // one of its own controls or as the press that dismisses it. Below the overlay
  // stack, because a row of it can open one.
  if(ui.settings.visible) {
    const SettingsOutcome outcome = handleSettingsClick(ui, x, y);
    carryOutSettingsRequest(ui, outcome.request);
    if(outcome.handled) return;
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

  if(pastePrimaryWherePointed(text, ui, layout, x, y, button)) return;

  // The panes' scrollbars, before the text under them: a thumb overlaps the
  // trailing edge of every line it covers, and a press on a handle has to move
  // the handle.
  if(pressPaneScrollbar(ui, layout.content, x, y, button)) return;

  if(pressSidebar(text, ui, layout.sidebar, x, y, button)) return;

  if(contains(layout.menuBar, x, y)) {
    if(pressWindowButton(ui, x, y, button)) return;
    return;
  }
  if(handleBreadcrumbClick(ui, layout.breadcrumb, x, y)) return;
  if(contains(layout.content, x, y)) {
    pressPage(text, ui, layout.content, x, y, button);
    return;
  }
}

void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button) {
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
  const auto drop = sidebarDropTargetAt(ui, x, y);
  if(drop.valid) {
    const auto target = drop.folder;
    const auto rootName = ui.state.catalog().root().filename().generic_string();
    if(ui.sidebar.drag.file) {
      // A companion lands in the files area it was dropped on, or in the files
      // directory of the notebook it was dropped on -- created if the notebook
      // has none yet, because "put this PDF with that notebook" is the gesture.
      const auto destination = !drop.filesDir.empty()
                                 ? drop.filesDir
                                 : target / std::filesystem::path(library::kFilesDirName);
      const auto& carried = ui.sidebar.drag.filePath;
      const auto name = carried.filename().generic_string();
      if(!ui.state.moveCompanion(carried, destination).empty()) {
        ui.sidebar.tree.reveal(destination);
        ui.status = "Moved " + name + " to " + destination.generic_string();
      } else if(carried.parent_path() != destination) {
        ui.status = "Could not move " + name;
      }
    } else if(!drop.filesDir.empty()) {
      // The one thing a files area does not take. A note there would vanish
      // from the tree, and a notebook there would stop being one.
      ui.status = ui.sidebar.drag.note ? "Notes live in notebooks, not with files"
                                       : "Notebooks cannot be filed with files";
    } else if(ui.sidebar.drag.note) {
      selectNoteById(ui, ui.sidebar.drag.noteId);
      if(ui.state.moveSelectedNoteToFolder(target)) {
        ui.sidebar.tree.reveal(target);
        ui.status = "Moved note to " + (target.empty() ? rootName : target.generic_string());
      } else {
        ui.status = "Move note failed";
      }
    } else if(ui.state.moveFolderInto(ui.sidebar.drag.folderPath, target)) {
      ui.sidebar.tree.reveal(target / ui.sidebar.drag.folderPath.filename());
      ui.status = "Moved notebook into " + (target.empty() ? rootName : target.generic_string());
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
    // Through the surface the press started on, not through whatever the pane
    // mode names now. See `SelectSurface`.
    switch(ui.textSelect.surface) {
      case SelectSurface::LivePage:
        ui.editor.selectRange(ui.textSelect.anchor, ui.livePage.offsetAt(x, y));
        ui.revealEditorCursor = true;
        break;
      case SelectSurface::ReadingPage:
        // No caret to reveal: the reading pane has none, and scrolling it to a
        // caret it does not draw would move the text out from under the drag.
        ui.editor.selectRange(ui.textSelect.anchor, ui.readingPage.offsetAt(x, y));
        break;
      case SelectSurface::RawPane: {
        const ShellLayout layout = shellLayout(ui, width, height);
        const Rect editorRect = contentPanes(ui, layout.content).editor;
        ui.editor.selectRange(ui.textSelect.anchor, editorIndexAtPoint(text, ui, editorRect, x, y));
        ui.revealEditorCursor = true;
        break;
      }
    }
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
        ui.sidebar.list.scrollTo(scrollFromThumbY(sidebarListRect(layout.sidebar), y, ui.pointer.scrollDragOffsetY,
                                                 ui.sidebar.list.maxScroll()));
        break;
      case ScrollDrag::RawPane: {
        const Rect editorRect = contentPanes(ui, layout.content).editor;
        ui.raw.list.scrollTo(scrollFromThumbY(editorWritingRect(editorRect), y, ui.pointer.scrollDragOffsetY,
                                              ui.raw.list.maxScroll()));
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
    const auto drop = sidebarDropTargetAt(ui, x, y);
    ui.sidebar.drag.dropRow = drop.valid ? std::optional<std::size_t> {drop.row}
                                         : std::optional<std::size_t> {};
    return;
  }
  if(ui.sidebar.resizing) {
    ui.state.editWorkspace().sidebarWidth =
      std::clamp(x, ui::kMinSidebarWidth, std::max(ui::kMinSidebarWidth, static_cast<float>(width) - 520.0f));
  }
}

}
