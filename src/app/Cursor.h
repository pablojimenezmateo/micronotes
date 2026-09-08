#pragma once

#include "ui/Rect.h"

namespace micronotes {
namespace ui {
class TextRenderer;
}

namespace app {

struct UiRuntime;
enum class CursorKind;

// What shape the pointer takes, for the window as it stands this frame.
//
// One function, and it has to stay one: the answer is a priority order --
// a drag in progress beats a resize gutter, a gutter beats the panel beside it,
// a scrollbar beats the row lying under it -- and an order spread over the
// surfaces it ranks is an order nobody can read. Every surface it asks is a
// `...HasControlAt` predicate derived from the same geometry that surface drew
// and clicks against, so the cursor cannot promise a click that would miss.
//
// It lived in Application.cpp, where it was the largest thing in the file that
// had nothing to do with running a window.
CursorKind classifyCursor(ui::TextRenderer& text, UiRuntime& ui, int width, int height);

}
}
