#include "ui/Tabs.h"

#include "ui/Metrics.h"

#include <algorithm>

namespace micronotes::ui {

std::vector<TabSlot> layoutTabs(const std::vector<std::string>& titles, Rect strip,
                                const std::function<int(std::string_view)>& measure) {
  std::vector<TabSlot> slots;
  if(titles.empty() || strip.w <= 0.0f) return slots;

  // Every tab is the same width, as a browser's are: tabs that resize as you
  // move between them mean the one you were about to click has moved.
  const float even = strip.w / static_cast<float>(titles.size());
  float width = std::clamp(even, kMinTabWidth, kMaxTabWidth);

  // A title that needs more room than the share it was given gets it, up to the
  // maximum, as long as every tab still fits.
  if(measure) {
    float widest = 0.0f;
    for(const auto& title : titles) {
      const float needed = static_cast<float>(measure(title)) + kTabClosePadding * 3.0f + kTabCloseSize;
      widest = std::max(widest, needed);
    }
    width = std::clamp(std::min(widest, even), kMinTabWidth, kMaxTabWidth);
  }

  float x = strip.x;
  for(std::size_t i = 0; i < titles.size(); ++i) {
    TabSlot slot;
    slot.index = i;
    slot.rect = {x, strip.y, width, strip.h};
    // Off the end of the strip: laid out anyway, so scrolling the strip is a
    // matter of which slots are drawn rather than a second layout.
    slot.visible = x + width <= strip.x + strip.w + 0.5f;
    slot.close = {
      x + width - kTabClosePadding - kTabCloseSize,
      strip.y + (strip.h - kTabCloseSize) / 2.0f,
      kTabCloseSize,
      kTabCloseSize,
    };
    slots.push_back(slot);
    x += width;
  }
  return slots;
}

Rect tabCloseHitRect(const TabSlot& slot) {
  return {
    slot.close.x - kTabCloseHitInflate,
    slot.close.y - kTabCloseHitInflate,
    slot.close.w + kTabCloseHitInflate * 2.0f,
    slot.close.h + kTabCloseHitInflate * 2.0f,
  };
}

}
