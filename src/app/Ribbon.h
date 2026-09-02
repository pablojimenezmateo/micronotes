#pragma once

#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/Rect.h"

#include <optional>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The icon rail down the leading edge of the window.
//
// It runs beside the sidebar rather than inside it, and it does not hide. That
// is the whole of its job: with every panel put away, this is still the way to
// a new note, to the switcher, and to the panels themselves. A shell whose only
// route back is a chord nobody has learnt yet is a shell people get stuck in.
//
// Every control here is an `ActionId` and nothing else. The rail runs no
// behaviour of its own, so a button cannot drift from the palette row and the
// keyboard binding that claim to do the same thing -- they are one entry in
// `ui::Actions`, drawn three ways.
//
// Which controls it shows and where they sit is `ui::ribbonLayout`, so the
// draw and the two hit tests below cannot disagree about it, and the rule for
// a window too short to hold them all can be tested without a renderer.
void drawRibbon(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// The action a click on the rail asked for, or nothing when it missed. Returned
// rather than run, because dispatch lives with the command table in
// Application.cpp and the rail has no business reaching into it.
std::optional<ui::ActionId> handleRibbonClick(UiRuntime& ui, ui::Rect rect, float x, float y);

// Whether the pointer is over a control, so the cursor can say so.
bool ribbonHasControlAt(UiRuntime& ui, ui::Rect rect, float x, float y);

}
