#include "app/KeyRouter.h"

#include "app/BlockMenus.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Desktop.h"
#include "app/Dismiss.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Fields.h"
#include "app/Folds.h"
#include "app/Notes.h"
#include "app/OverlayRouter.h"
#include "app/PageView.h"
#include "app/Prompts.h"
#include "app/SettingsDialog.h"
#include "app/Shell.h"
#include "app/SidebarModel.h"
#include "app/WikiLinks.h"
#include "core/editor/TextField.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "ui/Actions.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace micronotes::app {
void handleText(UiRuntime& ui, const char* input) {
  if(!input) return;
  if(ui.overlays.active()) {
    ui.overlays.handleText(input);
    return;
  }
  if(auto* field = focusedField(ui)) {
    // insert() replaces the selection, so a select-all followed by a keystroke
    // overwrites without any separate "all selected" flag to keep in step.
    field->editor.insert(input);
    syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    // Typing is text editing, so it takes the caret back from a block selection
    // rather than replacing whole blocks with a character.
    ui.blockSelection.clear();
    ui.editor.insert(input);
    // "[] " only becomes a real task marker once the space lands, so the check
    // is cheap and runs at most once per typed space.
    if(std::string_view(input).find(' ') != std::string_view::npos) {
      applyTransform(ui, doc::applyMarkdownShortcut);
    }
    ui.markEdited();
    ui.revealEditorCursor = true;
    // "/" opens the block inserter, but only where a block could start: mid-word
    // slashes belong to paths and URLs.
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live && std::string_view(input) == "/") {
      const std::size_t slash = ui.editor.cursor() - 1;
      const char before = slash == 0 ? '\n' : ui.editor.text()[slash - 1];
      if(before == '\n' || before == ' ' || before == '\t') openSlashMenu(ui, slash);
    }
    // The second "[" of a "[[" offers the notes it could mean. A single bracket
    // is left alone: it is how every ordinary link and every task marker starts.
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live && std::string_view(input) == "[") {
      const std::size_t bracket = ui.editor.cursor() - 1;
      if(bracket > 0 && ui.editor.text()[bracket - 1] == '[') openWikiMenu(ui, bracket - 1);
    }
  }
}

void handleKey(UiRuntime& ui, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod) {
  const SDL_Keymod currentMod = SDL_GetModState();
  const bool ctrl = ((mod | currentMod) & SDL_KMOD_CTRL) != 0;
  const bool shift = ((mod | currentMod) & SDL_KMOD_SHIFT) != 0;
  const bool alt = ((mod | currentMod) & SDL_KMOD_ALT) != 0;
  const auto shortcut = [&](SDL_Keycode keycode, SDL_Scancode code) {
    return ctrl && (key == keycode || scancode == code);
  };
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleKey(key, ctrl, shift, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  if(inputDebugEnabled()) {
    std::cerr << "input keydown"
              << " key=" << SDL_GetKeyName(key)
              << " keycode=0x" << std::hex << static_cast<Uint32>(key) << std::dec
              << " scancode=" << SDL_GetScancodeName(scancode)
              << " mod=0x" << std::hex << static_cast<Uint32>(mod)
              << " current_mod=0x" << static_cast<Uint32>(currentMod) << std::dec
              << " ctrl=" << ctrl
              << " focus=" << focusName(ui.focus)
              << " editor_selection=" << ui.editor.hasSelection()
              << " field_selection=" << (focusedField(ui) != nullptr && focusedField(ui)->editor.hasSelection())
              << "\n";
  }
  // Actions whose meaning does not depend on where the caret is are dispatched
  // straight from the binding table, so the chord lives in exactly one place.
  // The chain below is what is left to move: those branches read the focus, the
  // selection or the pane before deciding what the key meant, and each has to
  // be untangled before it can join this list.
  static constexpr ui::ActionId kBoundHere[] = {
    ui::ActionId::NextTab,
    ui::ActionId::PreviousTab,
    ui::ActionId::CloseTab,
    ui::ActionId::OpenInNewTab,
    ui::ActionId::ToggleSidebar,
    ui::ActionId::ToggleRightPanel,
    ui::ActionId::CycleRightPanel,
  };
  const ui::KeyChord pressed {key, ctrl, shift, alt};
  if(const auto* bound = ui::findActionForChord(pressed)) {
    if(std::find(std::begin(kBoundHere), std::end(kBoundHere), bound->id) != std::end(kBoundHere)) {
      performCommand(ui, std::string(bound->name));
      return;
    }
  }

  if(key == SDLK_F1) {
    openShortcutHelp(ui);
  } else if(shortcut(SDLK_COMMA, SDL_SCANCODE_COMMA)) {
    openSettings(ui);
  } else if(shortcut(SDLK_P, SDL_SCANCODE_P)) {
    // Ctrl+Shift+P is every command; Ctrl+P is the notes, which is the jump
    // people reach for a hundred times more often.
    if(shift) openCommandPalette(ui);
    else openNotePalette(ui, "jump-note", "Go to note");
  } else if(shortcut(SDLK_N, SDL_SCANCODE_N)) {
    createNote(ui);
  } else if(shortcut(SDLK_S, SDL_SCANCODE_S)) {
    saveCurrent(ui);
  } else if(shortcut(SDLK_R, SDL_SCANCODE_R)) {
    invalidateWikiNotes(ui);
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  } else if(shortcut(SDLK_T, SDL_SCANCODE_T)) {
    beginTagEdit(ui);
  } else if(shortcut(SDLK_A, SDL_SCANCODE_A)) {
    if(ui.focus == FocusArea::Editor) {
      ui.editor.selectAll();
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
    else if(auto* field = focusedField(ui)) {
      field->editor.selectAll();
      if(field->editor.hasSelection()) SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
    }
  } else if(shortcut(SDLK_C, SDL_SCANCODE_C)) {
    if(ui.focus == FocusArea::Editor && ui.blockSelection.active) {
      const auto [from, to] = blockSelectionCarets(ui);
      const EditorBlocks blocks(ui);
      const std::size_t start = blocks[doc::blockIndexAt(blocks, from)].start;
      const std::size_t end = blocks[doc::blockIndexAt(blocks, to)].end();
      ui.status = setClipboardText(std::string_view(ui.editor.text()).substr(start, end - start))
                    ? "Copied block" : "Copy failed: " + std::string(SDL_GetError());
    } else if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      ui.status = setClipboardText(ui.editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
      // Copies the actual selected range. It used to copy the whole field,
      // because the whole field was the only range that could be selected.
      ui.status = setClipboardText(field->editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_X, SDL_SCANCODE_X)) {
    if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      const bool copied = setClipboardText(ui.editor.selectedText());
      ui.editor.eraseSelection();
      ui.markEdited();
      ui.revealEditorCursor = true;
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
      const bool copied = setClipboardText(field->editor.selectedText());
      field->editor.eraseSelection();
      syncFocusedInput(ui);
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) {
    // Resolved through `shortcut`, which also accepts the scancode, so Ctrl+Z
    // still works on a layout where the Z key does not produce 'z'.
    if(auto* field = focusedField(ui)) {
      if(field->editor.undo()) syncFocusedInput(ui);
    } else if(ui.focus == FocusArea::Editor) {
      (void)undoEditorEdit(ui);
    }
  } else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) {
    if(auto* field = focusedField(ui)) {
      if(field->editor.redo()) syncFocusedInput(ui);
    } else if(ui.focus == FocusArea::Editor) {
      (void)redoEditorEdit(ui);
    }
  } else if(shortcut(SDLK_V, SDL_SCANCODE_V)) {
    if(focusedField(ui)) pasteClipboardIntoInput(ui);
    else if(ui.focus == FocusArea::Editor) {
      // Decide by what is actually on the clipboard: image data wins (image
      // copies often also expose an incidental text/plain target), otherwise
      // paste text. Shift forces plain text even when an image is present.
      if(shift) {
        if(!pasteClipboardText(ui)) pasteClipboardImage(ui);
      } else {
        if(!pasteClipboardImage(ui)) pasteClipboardText(ui);
      }
      ui.revealEditorCursor = true;
    }
  } else if(shortcut(SDLK_B, SDL_SCANCODE_B)) {
    wrapEditorSelection(ui, "**", "**", "Bold");
  } else if(shortcut(SDLK_I, SDL_SCANCODE_I)) {
    wrapEditorSelection(ui, "*", "*", "Italic");
  } else if(shortcut(SDLK_E, SDL_SCANCODE_E)) {
    wrapEditorSelection(ui, "`", "`", "Code");
  } else if(shortcut(SDLK_K, SDL_SCANCODE_K)) {
    // In the editor Ctrl+K makes a link out of the selection, as it does
    // everywhere else; outside it there is no selection to link, so it is the
    // jump the plan asked for.
    if(ui.focus == FocusArea::Editor) linkEditorSelection(ui);
    else openNotePalette(ui, "jump-note", "Go to note");
  } else if(shortcut(SDLK_PERIOD, SDL_SCANCODE_PERIOD)) {
    if(ui.focus == FocusArea::Editor) toggleFoldAt(ui, ui.editor.cursor());
  } else if(shortcut(SDLK_D, SDL_SCANCODE_D) && shift) {
    if(ui.focus == FocusArea::Editor) performBlockCommand(ui, "delete");
  } else if(shortcut(SDLK_D, SDL_SCANCODE_D)) {
    if(ui.focus == FocusArea::Editor) performBlockCommand(ui, "duplicate");
  } else if(shift && shortcut(SDLK_0, SDL_SCANCODE_0)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Paragraph, 0, "text");
  } else if(shift && shortcut(SDLK_1, SDL_SCANCODE_1)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 1, "heading 1");
  } else if(shift && shortcut(SDLK_2, SDL_SCANCODE_2)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 2, "heading 2");
  } else if(shift && shortcut(SDLK_3, SDL_SCANCODE_3)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 3, "heading 3");
  } else if(shift && shortcut(SDLK_7, SDL_SCANCODE_7)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Ordered, 0, "a numbered item");
  } else if(shift && shortcut(SDLK_8, SDL_SCANCODE_8)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Bullet, 0, "a bullet");
  } else if(shift && shortcut(SDLK_9, SDL_SCANCODE_9)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Todo, 0, "a task");
  } else if(shortcut(SDLK_1, SDL_SCANCODE_1)) {
    setPaneMode(ui, ui::PaneMode::Live);
  } else if(shortcut(SDLK_2, SDL_SCANCODE_2)) {
    setPaneMode(ui, ui::PaneMode::Editor);
  } else if(shortcut(SDLK_3, SDL_SCANCODE_3)) {
    setPaneMode(ui, ui::PaneMode::Viewer);
  } else if(shortcut(SDLK_4, SDL_SCANCODE_4)) {
    setPaneMode(ui, ui::PaneMode::Split);
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L) && shift) {
    const bool toDark = ui::themeMode() == ui::ThemeMode::Light;
    ui::setThemeMode(toDark ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = toDark ? "Dark theme" : "Light theme";
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L)) {
    cyclePaneMode(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F) && shift) {
    focusSearchAllNotes(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F)) {
    focusFindInNote(ui);
  } else if(key == SDLK_ESCAPE) {
    // One press undoes one narrowing; `app/Dismiss.h` owns which, and why.
    const Dismissed undid = dismissOne(ui);
    // With nothing narrowed, Esc belongs to whatever has focus: in the live
    // surface it steps out of the text onto the block, and the press after that
    // is the block selection `dismissOne` then finds.
    if(undid == Dismissed::Nothing && ui.focus == FocusArea::Editor &&
       ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      selectBlockAtCursor(ui);
    }
    // Leaving a tag filter lands in the tree that has just come back; every
    // other narrowing hands focus to the page.
    ui.focus = undid == Dismissed::TagFilter ? FocusArea::Folders : FocusArea::Editor;
  } else if(ui.focus == FocusArea::Search && (key == SDLK_DOWN || key == SDLK_UP)) {
    // The results are sidebar rows now, so walking them is the tree cursor: the
    // field keeps the typing and the list keeps the selection. A single-line
    // field has nothing else to do with Up and Down.
    moveTreeCursor(ui, key == SDLK_DOWN ? 1 : -1);
  } else if(auto* field = focusedField(ui)) {
    // Enter is the only key whose meaning depends on which field this is;
    // everything else -- arrows, word motion, Home/End, Backspace, Delete,
    // Ctrl+Z/Y -- is the same for all five and lives in one place.
    if(key == SDLK_RETURN) {
      switch(ui.focus) {
        case FocusArea::TagEditor: saveTags(ui); break;
        case FocusArea::RenameNote: saveRename(ui); break;
        case FocusArea::RenameFolder: saveFolderRename(ui); break;
        default: ui.focus = FocusArea::Editor; break;
      }
    } else {
      const auto result = editor::applyKeyToField(*field, key, ctrl, shift);
      if(result == editor::FieldKeyResult::Changed) syncFocusedInput(ui);
      else if(result == editor::FieldKeyResult::Moved && field->editor.hasSelection()) {
        SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
      }
    }
  } else if(ui.focus == FocusArea::Editor && ui.blockSelection.active) {
    // Selected blocks are objects: the arrows walk them, and one command acts
    // over the whole range.
    if(alt && (key == SDLK_UP || key == SDLK_DOWN)) {
      moveSelectedBlocks(ui, key == SDLK_UP ? -1 : 1);
    } else if(key == SDLK_UP || key == SDLK_DOWN) {
      moveBlockSelection(ui, key == SDLK_UP ? -1 : 1, shift);
    } else if(key == SDLK_BACKSPACE || key == SDLK_DELETE) {
      performBlockCommand(ui, "delete");
    } else if(key == SDLK_TAB) {
      applyTransform(ui, shift ? doc::outdent : doc::indent);
      syncBlockSelectionToEdit(ui);
    } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      // Enter puts the caret back into the first selected block's text.
      const EditorBlocks blocks(ui);
      const auto content = blocks[doc::blockIndexAt(blocks, blockSelectionCarets(ui).first)].contentStart();
      ui.blockSelection.clear();
      ui.editor.moveCursor(content);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_LEFT || key == SDLK_RIGHT) {
      ui.blockSelection.clear();
    }
  } else if(ui.focus == FocusArea::Editor) {
    const bool live = ui.state.workspace().paneMode() == ui::PaneMode::Live;
    // One visual row up or down: the live surface wraps, the raw editor does not.
    const auto rowStep = [&](int rows) {
      return live ? ui.livePage.rowRelative(ui.editor.cursor(), rows) : ui.editor.cursor();
    };
    if(key == SDLK_BACKSPACE) {
      if(ctrl) ui.editor.erasePreviousWord();
      // Against a block's first character, Backspace strips the block's marker
      // before it starts eating the block above.
      else if(ui.editor.hasSelection() || !applyTransform(ui, doc::outdentOrUnwrap)) ui.editor.erasePrevious();
      ui.markEdited();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DELETE) {
      if(ctrl) ui.editor.eraseNextWord();
      else ui.editor.eraseNext();
      ui.markEdited();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      if(ctrl) {
        if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
      } else if(ui.editor.hasSelection() ||
                (!applyTransform(ui, doc::closeFence) && !applyTransform(ui, doc::continueList))) {
        ui.editor.insert("\n");
        ui.markEdited();
        ui.revealEditorCursor = true;
      }
    } else if(key == SDLK_TAB) {
      if(shift) {
        applyTransform(ui, doc::outdent);
      } else if(!applyTransform(ui, doc::indent)) {
        ui.editor.insert("  ");
        ui.markEdited();
        ui.revealEditorCursor = true;
      }
    } else if(key == SDLK_LEFT) {
      if(ctrl) ui.editor.moveWordLeft(shift);
      else ui.editor.moveLeft(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RIGHT) {
      if(ctrl) ui.editor.moveWordRight(shift);
      else ui.editor.moveRight(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_UP) {
      if(alt) moveSelectedBlocks(ui, -1);
      else if(live) ui.editor.moveTo(rowStep(-1), shift);
      else ui.editor.moveLineUp(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DOWN) {
      if(alt) moveSelectedBlocks(ui, 1);
      else if(live) ui.editor.moveTo(rowStep(1), shift);
      else ui.editor.moveLineDown(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
      const int direction = key == SDLK_PAGEUP ? -1 : 1;
      if(live) {
        const int rows = static_cast<int>(std::max<std::size_t>(1, ui.livePage.rowsPerPage()));
        ui.editor.moveTo(rowStep(direction * rows), shift);
      } else {
        for(int i = 0; i < std::max(1, ui.raw.visibleRows); ++i) {
          if(direction < 0) ui.editor.moveLineUp(shift);
          else ui.editor.moveLineDown(shift);
        }
      }
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_HOME) {
      if(ctrl) ui.editor.moveDocumentStart(shift);
      else ui.editor.moveLineStart(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_END) {
      if(ctrl) ui.editor.moveDocumentEnd(shift);
      else ui.editor.moveLineEnd(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
  } else if(ui.focus == FocusArea::Folders) {
    if(key == SDLK_DOWN || key == SDLK_UP) moveTreeCursor(ui, key == SDLK_DOWN ? 1 : -1);
    else if(key == SDLK_RIGHT || key == SDLK_LEFT) expandTreeCursor(ui, key == SDLK_RIGHT);
    // Enter chooses the row the cursor is on; see `chooseSidebarCursorRow`.
    else if(key == SDLK_RETURN) ui.focus = chooseSidebarCursorRow(ui);
  }
}

}
