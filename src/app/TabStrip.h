#pragma once

#include "ui/TextRenderer.h"
#include "ui/Overlay.h"
#include "ui/Rect.h"
#include "ui/Tabs.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

namespace micronotes::app {

struct UiRuntime;

// A tab being carried to a new place in the strip.
//
// The move is deferred to the release rather than applied as the pointer goes:
// reordering live means the tab under the pointer keeps changing identity
// mid-gesture, so the strip shuffles under the hand and a drag that ends where
// it began still leaves the order different. Nothing here touches the model
// until the button comes up.
struct TabDrag {
  // Where the button went down, so the threshold can be measured, and whether
  // that has been passed. A press that never travels far enough stays a click.
  bool pressed = false;
  bool dragging = false;
  float pressX = 0.0f;
  float pressY = 0.0f;
  // The tab the press landed on, and the grip it took on it: `pressX` less that
  // tab's leading edge. Kept so the tab does not jump under the pointer the
  // moment the drag starts.
  std::size_t source = 0;
  float grabOffsetX = 0.0f;
  float tabWidth = 0.0f;
  // Where the pointer is now, and the gap the tab would drop into. Both are
  // written by the motion and read by the paint, so what is drawn and what a
  // release commits are one answer.
  float pointerX = 0.0f;
  std::size_t dropSlot = 0;

  void clear() { *this = {}; }
};

// What the strip drew, recorded as it drew it.
//
// The strip used to lay itself out again on every question about it: once for
// the paint, once for the click, once more for the cursor shape. That is not
// an efficiency complaint -- `layoutTabs` narrows every
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
  // The gesture in flight over it, if any.
  TabDrag drag;

  // What the *last frame* drew, thrown away at the top of the next one and
  // refilled if the strip draws again. Deliberately not the drag: that is a
  // gesture spanning many frames, and it is cleared by the release -- or by a
  // motion event that finds the button already up.
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

// The other two thirds of a tab drag. Motion says whether it took the pointer,
// so the router can stop there; the release commits whatever the drag resolved
// to and clears it. Both are safe to call with no drag in flight.
bool handleTabStripMotion(UiRuntime& ui, float x, float y);
void handleTabStripRelease(UiRuntime& ui);

// Moving between tabs and closing them.
void stepTab(UiRuntime& ui, int delta);
void closeActiveTab(UiRuntime& ui);

// Which tabs a bulk close takes, measured from the tab it was asked about.
//
// Four scopes rather than four functions because the only thing that differs
// between them is one comparison against that tab's index -- the save, the
// descending walk that keeps the indices valid, and the reload afterwards are
// the same work in all four, and were the part worth not writing four times.
enum class TabCloseScope { Others, ToRight, ToLeft, All };

// Closes every tab in `scope` around `index`, reports what it did in the status
// line, and says how many went. Pinned tabs are spared; see the comment on the
// implementation for why that is what pinning already meant.
std::size_t closeTabs(UiRuntime& ui, std::size_t index, TabCloseScope scope);

// The same, measured from the tab showing. The strip's own menu is about the
// tab it was opened on -- which is why a right click does not switch to one --
// and the menu bar and the palette have no such tab to name, so they mean this
// one.
void closeTabsAroundActive(UiRuntime& ui, TabCloseScope scope);

// Carries out a choice from a tab's own menu, and says whether it was one of
// its. The tab travels in the result's `value`, so a menu opened on one tab
// cannot act on another -- which is the whole point of not switching to a tab
// in order to right-click it.
bool handleTabMenuResult(UiRuntime& ui, const ui::OverlayResult& result);

}
