#pragma once

#include "ui/Rect.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>

// The find bar: a card floating at the top-right of the page, over the note it
// is searching.
//
// Ported from the sibling `../microide`, whose in-file find is the same object
// -- `ComputeFindWidgetLayout` and `RenderFindWidget` -- and which is the UI
// reference for this tree. What micronotes had instead was a text field spliced
// into the status line with a match *count* beside it: no way to step to a
// match, nothing marking which one you were on, and no room for an option
// toggle even if there had been one to offer.
//
// The geometry is one function shared by the paint and the hit test, for the
// reason microide's header gives: two copies of a button's rect is a button that
// moves out from under the pointer. Nothing about the bar is recorded as it is
// drawn, so a press works on a frame the bar has not been painted on yet.
namespace micronotes::app {

struct UiRuntime;

// The bar's toggles, in paint, hit-test and tooltip order. The same two
// microide's find widget leads with, in the same order and with the same
// labels, so the two applications read alike; regex is deliberately not among
// them -- see the note in `app/FindBar.cpp`.
enum class FindToggle : std::size_t {
  MatchCase = 0,
  WholeWord = 1,
  Count = 2
};

inline constexpr std::size_t kFindToggleCount = static_cast<std::size_t>(FindToggle::Count);

// Every rect the bar is made of. Empty when the bar is not showing, so a caller
// may hit-test unconditionally.
struct FindBarLayout {
  ui::Rect bar;
  ui::Rect field;
  std::array<ui::Rect, kFindToggleCount> toggles {};
  ui::Rect count;
  ui::Rect previous;
  ui::Rect next;
  ui::Rect close;
};

// Where the bar sits over a content pane. `showing` false gives an all-empty
// layout rather than a flag the caller has to remember to check.
FindBarLayout findBarLayout(ui::Rect content, bool showing, const ui::TextRenderer& text);

// The layout for the bar as this shell would draw it now.
FindBarLayout findBarLayout(const UiRuntime& ui, ui::Rect content, const ui::TextRenderer& text);

void drawFindBar(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect content);

// A press inside the bar. Returns whether the bar took it -- including a press
// on the card's own ground, which the bar swallows so a click just beside a
// button does not land in the note underneath.
bool pressFindBar(ui::TextRenderer& text, UiRuntime& ui, ui::Rect content, float x, float y,
                  Uint8 button);

// --- what the bar does ----------------------------------------------------

// Opens the bar and gives it the keyboard. With a selection in the note, the
// selection becomes the query: looking for "the thing I just highlighted" is
// what the key is reached for after, and re-typing it is the step nobody wants.
void openFindInNote(UiRuntime& ui);
// Opens the bar on the note just opened from a library search result, carrying
// the search box's query into it, exactly as ../microide's
// `OpenBufferSearchFromProjectSearchResult` carries a project-search term into
// the in-file find. Reaching a note through a search and then having to retype
// what you searched for to walk its matches is the step the port removes.
//
// Returns whether the bar was opened: it declines a note whose *body* does not
// hold the query, which a library search can produce and a grep cannot -- see
// the definition. The answer is what tells the caller where the keyboard went.
bool openFindFromSearch(UiRuntime& ui);

// Closes it, drops the highlights, and hands the keyboard back to the note.
void closeFindInNote(UiRuntime& ui);

// Brings `ui.find.matches` up to date with the buffer, the needle and the
// options. Cheap to call: it compares its memo key first, and every path that
// can move any of the three calls it.
void refreshFindMatches(UiRuntime& ui);

// Selects the active match in the buffer and asks every pane showing the note
// to scroll to it. Selecting rather than only scrolling: the pages draw the
// buffer's selection, so the match the reader is on reads as picked out without
// a second treatment, and closing the bar leaves it there to be copied.
void revealFindMatch(UiRuntime& ui);

// Steps to the next (+1) or previous (-1) match, wrapping at both ends, and
// scrolls whichever panes are showing to it. Does nothing with no matches.
void moveFindMatch(UiRuntime& ui, int delta);

// Flips one toggle and re-runs the search under it.
void toggleFindOption(UiRuntime& ui, FindToggle toggle);

// A key press while the find field has the keyboard, before it reaches the
// field itself. Returns whether the bar took it.
//
// Enter and the vertical arrows step the matches and Alt+C / Alt+W flip the
// toggles -- ../microide's chords, one per button, in button order. Everything
// else falls through to the field, which is what makes a plain `c` or `w` type
// rather than toggle. Stepping with Enter rather than committing is the one
// difference from the other four one-line fields, and it is why the find bar
// has a key handler at all: Enter in a search box means "show me the next one",
// not "I am done here".
bool handleFindBarKey(UiRuntime& ui, SDL_Keycode key, bool shift, bool alt);

// What the bar prints between the toggles and the arrows: "3 of 17", "No
// results", or nothing at all when no query has been typed. Its own function so
// the test can read it without a renderer.
std::string findMatchCountText(const UiRuntime& ui);

}
