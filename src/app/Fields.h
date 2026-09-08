#pragma once

#include "CoreAliases.h"

#include "app/Focus.h"
#include "app/TextFields.h"

#include "core/editor/TextField.h"

#include <span>

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

// One row per single-line field: which focus names it, where it lives on
// `TextFields`, and what committing it means.
//
// A table rather than three switches. `focusedField`, `caretStateKey` and
// `handleFieldKey`'s Enter arm each used to walk the same five `FocusArea`
// values by hand, and adding a sixth field was three edits with no compiler
// help. The one that got forgotten was `caretStateKey`, where the symptom is a
// caret that blinks through a burst of typing in the new field and nothing
// else -- which is the exact failure that key was introduced to prevent, so
// the shape reintroduced it once per field added.
//
// It is also the answer to what `FocusArea` is carrying. Eight values, five of
// which name a prompt; this table is that half of the enum written down, and
// the three values with no row are the three surfaces that are not fields.
struct FieldSpec {
  FocusArea focus;
  editor::TextField TextFields::*member;
  // What Enter does. Null for the fields whose commit is just giving the
  // keyboard back to the page.
  void (*commit)(UiRuntime& ui);
};

std::span<const FieldSpec> fieldSpecs();

// The field the focus names, or nullptr when the focus is not in one.
editor::TextField* focusedField(UiRuntime& ui);
const editor::TextField* focusedField(const UiRuntime& ui);

// What Enter means in the field the focus names. Null when the focus is not in
// a field, or when committing it means nothing but leaving it.
void (*fieldCommit(FocusArea focus))(UiRuntime&);

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
