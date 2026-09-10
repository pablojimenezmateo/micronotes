#pragma once

#include "app/PageView.h"

#include "doc/Layout.h"
#include "ui/Rect.h"

// The vocabulary `PageView`'s two translation units share.
//
// `PageView.cpp` lays a note out and answers questions about it;
// `PageViewPaint.cpp` draws it. What is left in common between them is the
// gutter the page keeps clear and the one conversion neither of them can put
// anywhere else, so those live here rather than in either file's anonymous
// namespace.
//
// It used to hold three more: `toTextStyle` and two `colorFor`s, which were
// one-line renames of `ui::textStyleFor` and `ui::inkFor` under the names the
// two sources had already been calling them by. A translation unit whose job
// is to rename three functions is one more hop to follow for no answer, and
// the run loop that called them is `ui/DocRuns.h` now. Both are header-inline
// for the reason `doc/Flow.h` is: this runs once per selection rect per frame,
// and Release carries no LTO.
//
// Internal to `PageView`: nothing outside its two sources includes this.
namespace micronotes::app::pageview {

// Clear space above the first block and below the last, and therefore what the
// page loses out of its rect when the scroll ceiling is computed.
inline constexpr float kContentTopPadding = 18.0f;

// A rect in document space, moved into window space.
inline ui::Rect toRect(const doc::Rect& rect, float originX, float originY) {
  return {rect.x + originX, rect.y + originY, rect.w, rect.h};
}

}
