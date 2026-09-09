#pragma once

#include "CoreAliases.h"

#include "app/Shell.h"
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

// The live surface: the note rendered as formatted, editable content, with the
// caret in it. `PageView` does the layout and the paint; this is the wiring
// between it and the application -- the hooks it calls back through, the fold
// predicates, and the per-frame view state.
namespace micronotes::app {

void drawLive(SDL_Renderer* renderer, ui::TextRenderer& text, ui::ImageCache& images,
              UiRuntime& ui, ui::Rect rect);

}
