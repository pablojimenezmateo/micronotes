#pragma once

#include "app/PageView.h"

#include "doc/Layout.h"
#include "ui/Draw.h"
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

// Reserved to the left of the content column for the hover handles. The column
// is centred when the page is wide enough and pushed right when it is not, so
// the affordances always have somewhere to live.
//
// Three affordances live here, left to right: insert, drag handle, and the
// disclosure control, which sits closest to the text because it belongs to the
// block rather than to the pointer.
inline constexpr float kGutterWidth = 78.0f;
inline constexpr float kInsertOffset = 70.0f;
inline constexpr float kHandleOffset = 48.0f;
inline constexpr float kFoldOffset = 22.0f;

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
