#pragma once

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The platform half of a window that draws its own controls.
//
// micronotes asks for a borderless window and paints the title bar itself, so
// two jobs the compositor would otherwise do fall to us: telling the display
// server which parts of the surface move or resize the window, and carrying out
// what a click on a drawn button asked for. Both need the SDL_Window, which is
// why they are here rather than beside the drawing in Chrome.cpp.

// The SDL hints that decide how input reaches a window, set before SDL_Init
// because that is when the video backend reads them. They live beside the
// hit test for the same reason it does: both are the display server's half of
// a window that draws its own controls, and both are invisible in the drawing
// code that depends on them.
void setInputHints();

// Makes `window` movable and resizable by its drawn chrome. Returns false when
// the platform refuses a hit test, having already put the decorations back and
// cleared `ui.customChrome` -- a borderless window nobody can move is worse
// than a decorated one, so the caller does not have to handle the failure.
//
// `context` must outlive the window: SDL keeps the pointer and calls back into
// it. Passing a temporary here is a use-after-free that only shows up when
// someone drags the window.
struct HitTestContext {
  UiRuntime* ui = nullptr;
  SDL_Renderer* renderer = nullptr;
};

bool installWindowHitTest(SDL_Window* window, SDL_Renderer* renderer, UiRuntime& ui,
                          HitTestContext& context);

// Carries out whatever the drawn buttons recorded this frame and clears it.
// Returns true when something happened, so the caller knows to repaint; sets
// `running` to false for a close. Also reconciles `ui.windowMaximized` with the
// window's real state, which the compositor can change without being asked.
bool applyPendingWindowAction(SDL_Window* window, UiRuntime& ui, bool& running);

// Records what a press at (x, y) on a drawn window control asked for. Returns
// true when the press was one, so the caller stops looking: the buttons sit in
// the title bar over the same strip the breadcrumb uses, and they take the
// click first.
bool pressWindowButton(UiRuntime& ui, float x, float y, Uint8 button);

}
