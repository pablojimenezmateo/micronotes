#pragma once

// The six editing verbs whose meaning is a function of the focus.
//
// `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+Z`, `Ctrl+Y` and `Ctrl+V` all mean "do
// this to whatever has the keyboard", and each answers it with the same
// two-or-three-way branch: the note buffer, a block selection in it, or
// whichever one-line field is focused. They were written out inside
// `handleKey`, which made them eighty-five of that router's hundred and
// eighty-one lines -- and `app/KeyRouter.h` is supposed to route, with the
// behaviour living in the surface that owns it.
//
// Being in the router also made them unreachable from anywhere else, and that
// was a live bug rather than a tidiness argument. `Undo` and `Redo` are Edit
// menu items, and their `ActionSpec::keyRunsIt` is false, so the key ran the
// router's branch while the menu ran `performCommand`'s -- which handled the
// note buffer and not a focused field. With the caret in a rename box or the
// find field, `Ctrl+Z` undid and choosing Undo from the menu did nothing at
// all. One verb, two implementations, and only one of them complete.
//
// So each is one function, both callers call it, and there is no second
// answer to disagree with.
namespace micronotes::app {

struct UiRuntime;

// Selects everything in whatever has focus, and publishes it as the primary
// selection so a middle click elsewhere pastes it.
void selectAllInFocus(UiRuntime& ui);

// Copies the selected text -- from the reading pane as well as the editor,
// because a selection you can see and cannot copy is worse than one you cannot
// make -- or the focused field's own selection.
void copySelectionInFocus(UiRuntime& ui);

// Copies the selection and erases it.
void cutSelectionInFocus(UiRuntime& ui);

// One step back or forward on whichever undo stack has focus: the field's own,
// or the note's.
void undoInFocus(UiRuntime& ui);
void redoInFocus(UiRuntime& ui);

// Pastes into whatever has focus. In the note, image data wins over text --
// an image copy often also exposes an incidental `text/plain` target -- unless
// `plainTextOnly` is set, which is what Shift means here.
void pasteInFocus(UiRuntime& ui, bool plainTextOnly);

}
