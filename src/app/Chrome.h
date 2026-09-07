#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <string_view>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The line along the bottom of the window: whether the note is saved, what just
// happened, which view you are in, and how much you have written.
//
// The strip that used to be its counterpart along the top is two things now: a
// menu bar (`app/MenuBar.h`), and the breadcrumb band over the page
// (`app/Breadcrumb.h`) that carries the trail down to the open note.
void drawStatus(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// A note's icon, or a drawn mark when no emoji face is installed. Shared with
// the sidebar and the note list, which name notes the same way the trail does.
void drawNoteIcon(SDL_Renderer* renderer, ui::TextRenderer& text, std::string_view icon, ui::Rect box,
                  SDL_Color color);

// What the status bar calls the current pane mode.
const char* paneModeName(microcore::ui::PaneMode mode);

}
