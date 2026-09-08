#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The band between the tab strip and the page: where the open note lives, and
// whether it is kept.
//
// It used to be a strip along the top of the *window*, sharing that strip with
// the window controls -- which meant the trail had to stop short of them, the
// star had to be placed against whatever they left, and the folder the note is
// in was written above the tree rather than above the note. The band belongs to
// the page's own column, and the crumbs now sit directly over the note they
// describe.
void drawBreadcrumb(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// A click on a crumb or on the star. Acted on here rather than returned: both
// are navigation inside the library rather than commands, and `ui.chrome.crumbs` --
// which is what a crumb *is* -- is recorded by the draw above.
bool handleBreadcrumbClick(UiRuntime& ui, ui::Rect rect, float x, float y);

// Whether the pointer is over a crumb or the star, so the cursor can say so.
bool breadcrumbHasControlAt(const UiRuntime& ui, ui::Rect rect, float x, float y);

}
