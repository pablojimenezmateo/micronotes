#pragma once

#include "ui/Draw.h"
#include "ui/Overlay.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The strip of open notes above the page.
void drawTabStrip(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// Returns whether the click landed on the strip. `button` distinguishes a
// middle click, which closes, from a left one, which opens.
//
// `text` is not decoration: a tab's width depends on how wide its title
// measures, so the hit test has to lay the strip out through the same measurer
// the draw does or it tests rects that are not the ones on screen.
// Whether the pointer is over something on the strip that answers a click: a
// tab, its close cross, or an overflow chevron with tabs actually hidden behind
// it. The cursor asks, so that a strip of controls says it is one.
bool tabStripHasControlAt(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);

bool handleTabStripClick(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y,
                         Uint8 button, bool ctrl);

// Moving between tabs and closing them.
void stepTab(UiRuntime& ui, int delta);
void closeActiveTab(UiRuntime& ui);

// Carries out a choice from a tab's own menu, and says whether it was one of
// its. The tab travels in the result's `value`, so a menu opened on one tab
// cannot act on another -- which is the whole point of not switching to a tab
// in order to right-click it.
bool handleTabMenuResult(UiRuntime& ui, const ui::OverlayResult& result);

}
