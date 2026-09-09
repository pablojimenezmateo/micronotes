#pragma once

#include "ui/TextRenderer.h"
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

// The box the text is actually drawn in. How far the pane can scroll is
// `ui.raw.list`, recorded by the draw: asking for it used to mean soft-wrapping
// the whole note, at four call sites, one of which was the wheel.
ui::Rect editorWritingRect(ui::Rect editorRect);

}
