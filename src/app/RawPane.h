#pragma once

#include "CoreAliases.h"

#include "core/editor/SoftWrap.h"
#include "ui/TextRenderer.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <vector>

namespace micronotes::app {

struct UiRuntime;

// The raw-Markdown pane: the escape hatch for anything the note page does
// not model. It is the older of the two presentation layers -- it soft-wraps
// the buffer itself and draws through the bool-flag text API -- and is kept
// apart so that replacing it is a matter of deleting one file.
void drawEditor(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);
std::size_t editorIndexAtPoint(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);
void placeEditorCursor(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);

// The pane's soft wrap of the buffer, memoised on the revision, the column and
// the text size.
//
// Exposed for the harness's shell lane, which is what put a number on it: the
// rewrap is the whole note on every keystroke, and before the lane existed that
// was a figure somebody had measured once by hand -- and had measured against a
// fixed-advance stub, which is why it was wrong by two orders of magnitude. The
// pane is the only surface in the shell with a wrap of its own: it shows the
// file as bytes, so its line breaks are the file's and not the layout's.
const std::vector<editor::SoftWrapRow>& rawPaneRows(ui::TextRenderer& text, UiRuntime& ui,
                                                    ui::Rect rect);

// The box the text is actually drawn in. How far the pane can scroll is
// `ui.raw.list`, recorded by the draw: asking for it used to mean soft-wrapping
// the whole note, at four call sites, one of which was the wheel.
ui::Rect editorWritingRect(ui::Rect editorRect);

}
