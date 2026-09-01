#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <string_view>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The panel to the right of the page: what the open note contains, rather than
// what the library contains. The first surface to live outside Application.cpp,
// which is the shape the rest of the shell is being moved into.
void drawRightPanel(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// Returns whether the click landed on something the panel owns.
bool handleRightPanelClick(UiRuntime& ui, ui::Rect rect, float x, float y);

// Showing and hiding a panel, and choosing what the right one shows.
void togglePanel(UiRuntime& ui, bool ui::WorkspaceModel::*panel, std::string_view name);
void cycleRightPanel(UiRuntime& ui);

}
