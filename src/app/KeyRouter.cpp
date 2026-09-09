#include "app/KeyRouter.h"
#include "app/InputDebug.h"

#include "app/KeySurfaces.h"

#include "app/BlockMenus.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Desktop.h"
#include "app/Dismiss.h"
#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Fields.h"
#include "app/FindBar.h"
#include "app/FocusedEdits.h"
#include "app/Folds.h"
#include "app/MenuBar.h"
#include "app/Notes.h"
#include "app/OverlayRouter.h"
#include "app/PageView.h"
#include "app/Prompts.h"
#include "app/SettingsPane.h"
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
  if(handleSettingsText(ui, input)) return;
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
    if(ui.paneMode() == ui::PaneMode::Live && std::string_view(input) == "/") {
      const std::size_t slash = ui.editor.cursor() - 1;
      const char before = slash == 0 ? '\n' : ui.editor.text()[slash - 1];
      if(before == '\n' || before == ' ' || before == '\t') openSlashMenu(ui, slash);
    }
    // The second "[" of a "[[" offers the notes it could mean. A single bracket
    // is left alone: it is how every ordinary link and every task marker starts.
    if(ui.paneMode() == ui::PaneMode::Live && std::string_view(input) == "[") {
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
  // The digit this key is, or 0. By scancode as well as keycode for the same
  // reason `shortcut` is: on a layout where Shift+1 does not produce '1', the
  // keycode is whatever the layout says and the scancode is still the 1 key.
  const auto digitPressed = [](SDL_Keycode code, SDL_Scancode scan) -> char {
    if(code >= SDLK_0 && code <= SDLK_9) return static_cast<char>('0' + (code - SDLK_0));
    if(scan == SDL_SCANCODE_0) return '0';
    if(scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9) {
      return static_cast<char>('1' + (scan - SDL_SCANCODE_1));
    }
    return 0;
  };
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleKey(key, ctrl, shift, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  // The card is modal and takes every key while it is open, below the overlay
  // stack for the reason the press router gives.
  if(ui.settings.visible) {
    const SettingsOutcome outcome = handleSettingsKey(ui, key, ctrl, shift);
    carryOutSettingsRequest(ui, outcome.request);
    if(outcome.handled) return;
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
  // Actions the key alone can run are dispatched straight from the binding
  // table, so a chord is spelled in exactly one place. `ActionSpec::keyRunsIt`
  // is what says which those are; the chain below is the rest, and every branch
  // in it reads the focus, the selection or the pane before deciding what the
  // key meant.
  //
  // This was an allowlist of seven ids here, with twenty-five more chords
  // written out again as `shortcut(SDLK_x, SDL_SCANCODE_x)` branches -- and
  // three of the table's bindings (`F2`, `Ctrl+Q`, and `Ctrl+O` as the alias
  // for the note switcher) reached no branch at all, so they were advertised in
  // the palette and the shortcut list while doing nothing.
  // Before the binding table, because `Alt` is a modifier in it: a chord that
  // happens to spell a mnemonic must not be shadowed, and none does -- every
  // `Alt` binding micronotes has also holds `Ctrl`, which `openMenuByKey`
  // refuses.
  if(openMenuByKey(ui, key, ctrl, shift, alt)) return;

  if(const auto* bound = ui::findActionForKey(key, scancode, ctrl, shift, alt); bound && bound->keyRunsIt) {
    performCommand(ui, std::string(bound->name));
    return;
  }

  if(shortcut(SDLK_A, SDL_SCANCODE_A)) selectAllInFocus(ui);
  else if(shortcut(SDLK_C, SDL_SCANCODE_C)) copySelectionInFocus(ui);
  else if(shortcut(SDLK_X, SDL_SCANCODE_X)) cutSelectionInFocus(ui);
  // Resolved through `shortcut`, which also accepts the scancode, so Ctrl+Z
  // still works on a layout where the Z key does not produce 'z'.
  else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) undoInFocus(ui);
  else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) redoInFocus(ui);
  else if(shortcut(SDLK_V, SDL_SCANCODE_V)) pasteInFocus(ui, /*plainTextOnly=*/shift);
  else if(shortcut(SDLK_K, SDL_SCANCODE_K)) {
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
  } else if(shift && ctrl && blockKindForChordDigit(digitPressed(key, scancode))) {
    // The digit, the shape it makes and the label it reports all come from
    // `blockKinds()`, which is also what the block menu and the slash menu
    // read. This used to be seven branches naming the kind, the level and the
    // label again -- and the labels had drifted from the table's.
    performBlockCommand(ui, blockKindForChordDigit(digitPressed(key, scancode))->id);
  } else if(key == SDLK_ESCAPE) {
    // One press undoes one narrowing; `app/Dismiss.h` owns which, and why.
    const Dismissed undid = dismissOne(ui);
    // With nothing narrowed, Esc belongs to whatever has focus: in the live
    // surface it steps out of the text onto the block, and the press after that
    // is the block selection `dismissOne` then finds.
    if(undid == Dismissed::Nothing && ui.focus == FocusArea::Editor &&
       ui.paneMode() == ui::PaneMode::Live) {
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
  } else if(ui.focus == FocusArea::Find && handleFindBarKey(ui, key, shift, alt)) {
    // Taken by the bar: Enter steps to the next match rather than leaving the
    // field, and the option chords flip a toggle rather than typing a letter.
  } else if(auto* field = focusedField(ui)) {
    handleFieldKey(ui, *field, key, ctrl, shift);
  } else if(ui.focus == FocusArea::Editor && ui.blockSelection.active) {
    handleBlockSelectionKey(ui, key, shift, alt);
  } else if(ui.focus == FocusArea::Editor) {
    handleEditorKey(ui, key, ctrl, shift, alt);
  } else if(ui.focus == FocusArea::Folders) {
    handleSidebarKey(ui, key);
  }
}

}
