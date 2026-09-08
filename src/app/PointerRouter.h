#pragma once

#include "ui/Draw.h"

#include <SDL3/SDL.h>

// What a pointer event means, given what is under it and what is being dragged.
//
// Three entry points for the three phases of a gesture, and the split matters:
// a press decides *what* is being grabbed, a motion answers to whatever was
// grabbed, and a release commits it. Two of them were `static` in
// Application.cpp; the third was not a function at all -- the whole of the drag
// routing lived inside the event loop's `SDL_EVENT_MOUSE_MOTION` arm, so the
// only way to ask what a drag did was to run the app.
//
// The press path is ordered outside-in: an overlay, then the menu bar's popup,
// then the tab strip, the panels, and last the page. That order is what stops a
// click on a menu item from also landing on the tree row underneath it, and it
// is the reason this is a chain and not a set of independent hit tests.
namespace micronotes::app {

struct UiRuntime;

// A press. `text` because most of the hit tests measure something: a tab's
// width, a field's caret, a breadcrumb's crumb.
void handleMouse(ui::TextRenderer& text, UiRuntime& ui, float x, float y, Uint8 button,
                 int width, int height);

// A release: commits a block drag, a note or notebook drop, and drops every
// "still dragging" flag.
//
// No window size, unlike the other two: everything it hit-tests is a rect the
// frame recorded, so it has no layout to compute.
void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button);

// A motion. Answers to whichever gesture is in flight -- an open menu, a text
// selection, a scrollbar thumb, a block or a sidebar row being dragged, the
// sidebar's own edge -- and to none of them when nothing is being held.
void handleMouseMotion(ui::TextRenderer& text, UiRuntime& ui, float x, float y,
                       int width, int height);

}
