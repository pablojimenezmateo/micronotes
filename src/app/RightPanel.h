#pragma once

#include "app/PanelState.h"
#include "ui/TextRenderer.h"
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
// where it gets its block partition: it borrows the one the reading page splices,
// and that borrow only hits if the page has already laid out this revision --
// which is why `drawApp` draws this panel after the content. That is an
// ordering dependency between two files, and the counters are the only thing
// that can see it, so it needs a test that can reach in and read them.
const std::vector<ui::OutlineEntry>& outlineFor(UiRuntime& ui);

// The panel's other two views -- the backlinks and the tags -- memoised on the
// note and the library's revision.
//
// Exposed for the same kind of reason as the outline above, and it is again
// about a *key* rather than about what is drawn. A save bumps the library
// revision, so this memo misses once per save while the Links view is showing,
// and the miss runs a SQLite query -- which since TD-48 is also what runs the
// note's deferred index write. That is a cost nothing could see from outside
// this file, and the shell lane reads it here.
const RightPanelState::LibraryViews& libraryViewsFor(UiRuntime& ui);

// Returns whether the click landed on something the panel owns.
// Whether the pointer is over something in the panel that answers a click: one
// of its view tabs, its scrollbar thumb, or a backlink, tag or outline row.
// Asked by the cursor, and derived from the same row lists the click walks.
bool rightPanelHasControlAt(UiRuntime& ui, const ui::TextRenderer& text, ui::Rect rect,
                            float x, float y);

bool handleRightPanelClick(UiRuntime& ui, const ui::TextRenderer& text, ui::Rect rect, float x, float y);

// Showing and hiding a panel, and choosing what the right one shows.
void togglePanel(UiRuntime& ui, bool ui::WorkspaceModel::*panel, std::string_view name);
void cycleRightPanel(UiRuntime& ui);

}
