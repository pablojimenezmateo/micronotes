#pragma once

#include "CoreAliases.h"

#include "app/Focus.h"
#include "app/TextFields.h"

#include "core/editor/TextField.h"
#include "ui/Rect.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <span>
#include <string_view>

// The five one-line text fields the shell keeps -- search, find, tag, rename,
// folder rename -- as one thing rather than five.
//
// Which of them is live is a function of `ui.focus`, and *every* key, paste and
// middle click that reaches a field has to ask that question first. It was
// asked by a `static` in Application.cpp, so nothing outside that file could
// route a keystroke to a field; the clipboard paths, which are otherwise
// nothing to do with focus, had to live there for the same reason.
//
// Focusing one of these fields is never only a focus change -- the search box
// has to hand its query to the library, and a caller that sets `ui.focus` by
// hand gets a box that looks live and filters nothing -- so the verb for it is
// here beside the table. The find box's is `openFindInNote` in
// `app/FindBar.h`, with the rest of that surface.
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
// the library query and the note cursor; the find box re-runs its search over
// the note. Called by every path that edits a field, which is what keeps the
// two in step.
void syncFocusedInput(UiRuntime& ui);

// Focusing a search field is an action like any other, so the key and the
// palette row run the same code rather than two copies that drift.
void focusSearchAllNotes(UiRuntime& ui);

// Paints a single-line field: the selection band, then the text scrolled so
// the caret is visible, then the caret. The fields this replaced drew only the
// string, which is why they had no visible insertion point, no selection, and
// no way to reach text past the right edge.
//
// Here rather than in `app/Sidebar.h`, where it was written for the search box
// and then stayed. It is not a thing about the sidebar: it is how *any* of the
// five fields in this file is drawn, and the find bar reaching for it would
// otherwise have had to include the sidebar to paint its own query box.
void drawTextField(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                   editor::TextField& field, ui::Rect box, bool focused,
                   std::string_view placeholder);

// Byte offset in `field` under a pointer at window x, for a field drawn in
// `box`. Always a code point boundary.
std::size_t fieldOffsetAtX(const ui::TextRenderer& text, const editor::TextField& field,
                           ui::Rect box, float x);

}
