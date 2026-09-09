#pragma once

#include "app/Shell.h"
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

// The reading pane: the open note with no caret, no gutter and no toolbar.
//
// It used to be a second renderer for the same Markdown -- its own md4c parse,
// its own indent step, its own quote gutter, its own callout box, its own task
// checkbox, its own code block, its own table, its own image scaling -- and a
// screenshot of one 675-byte note against the live surface found six places
// where the two had drifted. Every one of them had to be fixed twice, and four
// of them were found by somebody comparing pictures.
//
// There is one renderer now. This file is `PageView` with three flags off; what
// it still owns is the wiring `PageView` cannot have, because the page holds a
// buffer rather than a library: how a note's images turn into textures.
namespace micronotes::app {

void drawReading(SDL_Renderer* renderer, ui::TextRenderer& text, ui::ImageCache& images,
                 UiRuntime& ui, ui::Rect rect);

}
