#pragma once

#include "ui/TextRenderer.h"
#include "ui/Overlay.h"
#include "ui/Rect.h"
#include "ui/Tabs.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace micronotes::app {

struct UiRuntime;

// What the strip drew, recorded as it drew it.
//
// The strip used to lay itself out again on every question about it: once for
// the paint, once for the click, once more for the cursor shape. That is
// `TD-20`, and it is not an efficiency complaint -- `layoutTabs` narrows every
// tab to the widest title when that is less than an even share of the strip, so
// its result depends on the text measurer it is handed, and three callers each
// deciding to call it the same way is three chances not to. One of them did
// not: `handleTabStripClick` passed `nullptr`, on a comment asserting the
// geometry was a pure function of the titles and the strip, and with two short
// note names in a wide strip the drawn tabs were a third of the width the hit
// test believed. A click on the second tab opened the first.
//
// So the strip is now the shape the sidebar has: the draw builds the list, and
// everything else walks what was drawn. There is no second call to disagree
// with, and the titles -- a `findNote` per tab -- are built once a frame rather
// than three times.
struct TabStripState {
  // The band the strip occupied. The strip swallows every click inside it --
  // the empty run past the last tab included, because a click there must not
  // fall through to the note -- so "was this in the strip" is a question about
  // the band and not about any tab in it. Empty when the strip was not drawn,
  // which is how a hidden strip answers nothing.
  ui::Rect rect;
  ui::TabStripLayout layout;
  // Kept beside the slots because a slot names its tab by index, and both the
  // tooltip and the close button's label need the title itself.
  std::vector<std::string> titles;

  void clear() {
    layout = {};
    titles.clear();
  }
};

// The strip of open notes above the page. Records what it drew into
// `ui.tabStrip`; the two hit tests below read that rather than laying out
// again, so a strip that has not been drawn answers no clicks -- which is the
// same answer as "there is no strip", and is what the shell means when it hides
// it.
void drawTabStrip(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// Whether the pointer is over something on the strip that answers a click: a
// tab, its close cross, or an overflow chevron with tabs actually hidden behind
// it. The cursor asks, so that a strip of controls says it is one.
bool tabStripHasControlAt(const UiRuntime& ui, float x, float y);

// Returns whether the click landed on the strip. `button` distinguishes a
// middle click, which closes, from a left one, which opens.
bool handleTabStripClick(UiRuntime& ui, float x, float y, Uint8 button, bool ctrl);

// Moving between tabs and closing them.
void stepTab(UiRuntime& ui, int delta);
void closeActiveTab(UiRuntime& ui);

// Carries out a choice from a tab's own menu, and says whether it was one of
// its. The tab travels in the result's `value`, so a menu opened on one tab
// cannot act on another -- which is the whole point of not switching to a tab
// in order to right-click it.
bool handleTabMenuResult(UiRuntime& ui, const ui::OverlayResult& result);

}
