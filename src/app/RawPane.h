#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <cstddef>

namespace micronotes::app {

struct UiRuntime;

// The raw-Markdown pane: the escape hatch for anything the live surface does
// not model. It is the older of the two presentation layers -- it soft-wraps
// the buffer itself and draws through the bool-flag text API -- and is kept
// apart so that replacing it is a matter of deleting one file.
void drawEditor(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);
std::size_t editorIndexAtPoint(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);
void placeEditorCursor(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);

// How far the pane can scroll, and the box the text is actually drawn in.
int editorMaxScroll(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);
ui::Rect editorWritingRect(ui::Rect editorRect);

}
