#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <string_view>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The two strips that frame the page: the trail of notebooks down to the open
// note, and the line along the bottom that says where you are and how to get
// somewhere else.
void drawBreadcrumbs(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);
void drawStatus(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// A note's icon, or a drawn mark when no emoji face is installed. Shared with
// the sidebar and the note list, which name notes the same way the trail does.
void drawNoteIcon(SDL_Renderer* renderer, ui::TextRenderer& text, std::string_view icon, ui::Rect box,
                  SDL_Color color);

// What the status bar calls the current pane mode.
const char* paneModeName(microcore::ui::PaneMode mode);

}
