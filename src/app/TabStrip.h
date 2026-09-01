#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The strip of open notes above the page.
void drawTabStrip(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// Returns whether the click landed on the strip. `button` distinguishes a
// middle click, which closes, from a left one, which opens.
bool handleTabStripClick(UiRuntime& ui, ui::Rect rect, float x, float y, Uint8 button, bool ctrl);

// Moving between tabs and closing them.
void stepTab(UiRuntime& ui, int delta);
void closeActiveTab(UiRuntime& ui);

}
