#pragma once

#include "ui/Rect.h"

#include <string>

namespace micronotes::ui {

// What the pointer is resting on, and what to say about it.
//
// Exactly one of these is resolved per frame, so the paint, the redraw and the
// cursor all agree about what is showing and where. Resolving per call site
// instead is how two tooltips end up on screen at once.
struct HoverTooltip {
  std::string text;
  // The control being described, not the pointer. A tooltip anchored to the
  // pointer jitters while the pointer moves inside one button; anchored to the
  // control it stays still, which is what makes it readable.
  Rect anchor;

  bool showing() const {
    return !text.empty();
  }
};

// Where the card goes: centred on the control, below it when there is room and
// flipped above when there is not, and clamped inside `bounds` either way so it
// is never half off the window.
Rect placeTooltip(Rect anchor, float width, float height, Rect bounds);

}
