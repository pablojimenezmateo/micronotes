#pragma once

#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <string_view>

#include <SDL3/SDL.h>

// What is left of the window's furniture once the strips have been given their
// own homes.
//
// It was the status bar plus these two, and the bar has gone to
// `app/StatusBar.h` -- what remains is a glyph three surfaces draw and the one
// word each of them uses for the pane mode. The strip along the top is two
// things: a menu bar (`app/MenuBar.h`) and the breadcrumb band over the page
// (`app/Breadcrumb.h`).
namespace micronotes::app {

// A note's icon, or the page mark a note with no icon wears. Shared with the
// sidebar and the note list, which name notes the same way the trail does.
void drawNoteIcon(SDL_Renderer* renderer, std::string_view icon, ui::Rect box, SDL_Color color);

// What the status bar calls the current pane mode.
const char* paneModeName(microcore::ui::PaneMode mode);

}
