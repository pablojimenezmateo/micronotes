#pragma once

#include "app/Cursor.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <functional>

// Which surface an SDL event goes to, and in what order.
//
// The chain per event *kind*, above the chains that route a press and a
// keystroke once they are known to be one (`app/PointerRouter.h`,
// `app/KeyRouter.h`). It lived in the run loop, which made
// `src/app/Application.cpp` the window, the renderer, the wait **and** the
// dispatch -- and the file's line budget is what said so out loud.
//
// The order inside is not arbitrary. An open menu owns the keyboard before the
// key chain sees it; the pointer's position is recorded before the handler that
// reads it; and the cursor shape is settled after every pointer event rather
// than at the top of the next frame, because a shape asked for on a frame that
// draws nothing is a shape nobody sees.
namespace micronotes::app {

struct UiRuntime;

// What the loop has to do about an event it handed over.
struct EventOutcome {
  // The window asked to close.
  bool quit = false;
  // The display scale may have moved, which only the loop can act on: it owns
  // the renderer's scale and the face cache that goes with it.
  bool displayScaleChanged = false;
};

// Routes one event. `width` and `height` are the window as the drain began, so
// every event in one drain is routed against one geometry.
EventOutcome routeEvent(const SDL_Event& event, ui::TextRenderer& text, UiRuntime& ui,
                        SystemCursors& cursors, int width, int height);

}
