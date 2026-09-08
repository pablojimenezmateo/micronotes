#pragma once

#include "ui/Rect.h"
#include "ui/Tooltip.h"

#include <string>
#include <utility>

// Where the pointer is, and what it has hold of.
//
// The position was two bare floats on the runtime with the tooltip logic
// written against them, and "which scrollbar is being dragged, and from where"
// was two more fields three dozen lines away. They belong together because
// they are read together: every frame asks "is the pointer inside this control"
// and every motion asks "is the pointer still holding something".
namespace micronotes::app {

// Which scrollbar thumb a drag has hold of.
enum class ScrollDrag {
  None,
  Live,
  RawPane,
  Reading,
  // The sidebar's own scrollbar. It had none: the old bar was a 3px hairline
  // with a 5px thumb, which nobody would try to grab, so nothing answered when
  // they did. A 10px track with a thumb filling it reads as a handle, and a
  // handle that does not move when pulled is worse than no handle.
  Sidebar
};

struct PointerState {
  // Off-window until the first event, so nothing is "hovered" before the
  // pointer has been anywhere.
  float x = -1;
  float y = -1;

  ScrollDrag scrollDrag = ScrollDrag::None;
  // Where inside the thumb it was grabbed, so the thumb does not jump to centre
  // itself under the pointer on the first pixel of the drag.
  float scrollDragOffsetY = 0.0f;

  // What the pointer is resting on. Cleared at the start of a frame and set by
  // whichever surface the pointer turns out to be over, so a frame can never
  // end up with two tooltips resolved.
  ui::HoverTooltip tooltip;

  bool over(ui::Rect control) const { return ui::contains(control, x, y); }

  // Offers a tooltip for `control` when the pointer is inside it. The last
  // caller wins, which is the innermost surface: a tooltip on a tab's close
  // button should beat the one on the tab.
  void offerTooltip(ui::Rect control, std::string text) {
    if(!over(control)) return;
    tooltip = {std::move(text), control};
  }
};

}
