#include "ui/Tooltip.h"

#include "ui/Metrics.h"

#include <algorithm>

namespace micronotes::ui {

Rect placeTooltip(Rect anchor, float width, float height, Rect bounds) {
  Rect card {
    anchor.x + (anchor.w - width) / 2.0f,
    anchor.y + anchor.h + kTooltipGap,
    width,
    height,
  };
  // Below by preference, because that is where the pointer is not.
  if(card.y + card.h > bounds.y + bounds.h) {
    card.y = anchor.y - height - kTooltipGap;
  }
  card.x = std::clamp(card.x, bounds.x, std::max(bounds.x, bounds.x + bounds.w - width));
  card.y = std::clamp(card.y, bounds.y, std::max(bounds.y, bounds.y + bounds.h - height));
  return card;
}

}
