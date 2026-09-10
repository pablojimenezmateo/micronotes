#pragma once

#include "app/PageView.h"

#include "doc/Layout.h"
#include "ui/TextRenderer.h"
#include "ui/Metrics.h"

#include <SDL3/SDL.h>

// The vocabulary `PageView`'s two translation units share.
//
// `PageView.cpp` lays a note out and answers questions about it; `PageView
// Paint.cpp` draws it. Both turn a `doc::` value into something the renderer
// understands -- a run's style, a role's colour, a document rect in window
// coordinates -- and both place things in the gutter, so those live here rather
// than in either file's anonymous namespace.
//
// Internal to `PageView`: nothing outside its two sources includes this.
namespace micronotes::app::pageview {

// Clear space above the first block and below the last, and therefore what the
// page loses out of its rect when the scroll ceiling is computed.
inline constexpr float kContentTopPadding = 18.0f;

ui::TextStyle toTextStyle(const doc::RunStyle& style);

SDL_Color colorFor(doc::TextRole role);
// A quote reads as someone else's words, so its body sits a step back from the
// page's own text. Markers and links keep their own roles.
SDL_Color colorFor(doc::TextRole role, doc::BlockKind kind);

// A rect in document space, moved into window space.
ui::Rect toRect(const doc::Rect& rect, float originX, float originY);

}
