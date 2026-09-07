#pragma once

#include "ui/Draw.h"
#include "ui/Outline.h"
#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The panel to the right of the page: what the open note contains, rather than
// what the library contains. The first surface to live outside Application.cpp,
// which is the shape the rest of the shell is being moved into.
void drawRightPanel(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// The outline of the open buffer, memoised on the editor's revision.
//
// Exposed because the thing worth testing about it is not what it draws but
// where it gets its block partition: it borrows the one the live page splices,
// and that borrow only hits if the page has already laid out this revision --
// which is why `drawApp` draws this panel after the content. That is an
// ordering dependency between two files, and the counters are the only thing
// that can see it, so it needs a test that can reach in and read them.
const std::vector<ui::OutlineEntry>& outlineFor(UiRuntime& ui);

// Returns whether the click landed on something the panel owns.
bool handleRightPanelClick(UiRuntime& ui, const ui::TextRenderer& text, ui::Rect rect, float x, float y);

// Showing and hiding a panel, and choosing what the right one shows.
void togglePanel(UiRuntime& ui, bool ui::WorkspaceModel::*panel, std::string_view name);
void cycleRightPanel(UiRuntime& ui);

}
