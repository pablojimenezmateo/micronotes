#pragma once

#include <SDL3/SDL.h>

#include <functional>

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

// The app's window, created unmapped.
//
// Borderless at creation rather than SDL_SetWindowBordered afterwards: on
// Wayland the decoration is a compositor-side object, so asking for one and
// then retracting it costs a blocking round-trip to the display server.
//
// Hidden at creation for the same reason, one step further out. A window is
// mapped the moment it exists, and everything between creating it and the
// first present -- the renderer, the font faces, the library scan, the first
// layout -- would happen with an empty window already on screen. That is the
// black rectangle that used to appear before the app did. Nothing maps this
// one until `revealWindow`.
//
// Returns null and reports why on failure, as SDL_CreateWindow does.
SDL_Window* createAppWindow(int width, int height);

// Maps the window, once, already painted.
//
// `drawFrame(width, height)` is called twice: once into the window nobody can
// see yet, which is what makes the window's one and only appearance an
// appearance of the app, and once straight after the map. The first call is the
// expensive one -- cold glyph atlas, cold image cache, first layout of the open
// note -- so the second is a warm redraw, and it is the one that guarantees
// pixels: a present to a window the display server has not mapped yet is
// allowed to be dropped.
void revealWindow(SDL_Window* window, const std::function<void(int, int)>& drawFrame);

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
