#pragma once

#include "app/Application.h"
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

// One frame of the whole window, and the ordering rules that make it correct.
//
// Two of those rules are load-bearing and neither is obvious from any one
// surface:
//
//   * the right panel is drawn *after* the content, because its outline borrows
//     the block partition the live page splices during its own layout, and
//     asking one revision early costs a whole-note rescan per keystroke with
//     identical pixels. `ArchitectureTests` pins the order.
//   * the caret's phase is settled before anything draws one, and recorded, so
//     the event loop can tell when the blink has flipped underneath it.
//
// Every surface is timed separately. Before that, the page was the only
// instrumented part of a frame, so a frame whose cost was the sidebar or the
// chrome showed up as time that went nowhere.
namespace micronotes::app {

struct UiRuntime;

void drawApp(SDL_Renderer* renderer, ui::TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
             int width, int height);

// Draws into an offscreen capture and writes it out. `--screenshot`'s whole
// implementation, and the reason `drawApp` takes its size rather than reading
// the window.
int captureFrame(SDL_Renderer* renderer, ui::TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
                 const ApplicationOptions& options);

}
