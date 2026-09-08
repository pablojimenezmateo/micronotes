#pragma once

#include "CoreAliases.h"

#include "core/editor/TextField.h"

// The five one-line text fields the shell keeps -- search, find, tag, rename,
// folder rename -- as one thing rather than five.
//
// Which of them is live is a function of `ui.focus`, and *every* key, paste and
// middle click that reaches a field has to ask that question first. It was
// asked by a `static` in Application.cpp, so nothing outside that file could
// route a keystroke to a field; the clipboard paths, which are otherwise
// nothing to do with focus, had to live there for the same reason.
//
// The two "focus a search box" verbs are here as well, because focusing one of
// these fields is never only a focus change: the find box recounts its matches
// and the search box hands its query to the library, and a caller that sets
// `ui.focus` by hand gets neither.
namespace micronotes::app {

struct UiRuntime;

// The field the focus names, or nullptr when the focus is not in one.
editor::TextField* focusedField(UiRuntime& ui);

// What the shell owes the field after its text changed. The search box drives
// the library query and the note cursor; the find box recounts its matches.
// Called by every path that edits a field, which is what keeps the two in step.
void syncFocusedInput(UiRuntime& ui);

// How many times the find box's needle occurs in the open note, as the status
// line says it.
void updateFindStatus(UiRuntime& ui);

// Focusing a search field is an action like any other, so the key and the
// palette row run the same code rather than two copies that drift.
void focusFindInNote(UiRuntime& ui);
void focusSearchAllNotes(UiRuntime& ui);

}
