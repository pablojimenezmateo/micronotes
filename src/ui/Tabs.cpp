#include "ui/Tabs.h"

#include "ui/Metrics.h"

#include <algorithm>

namespace micronotes::ui {

std::vector<TabSlot> layoutTabs(const std::vector<std::string>& titles, Rect strip,
                                const std::function<int(std::string_view)>& measure,
                                std::size_t activeTab) {
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

  // The window of tabs to show. Whole tabs only -- half a tab at the edge is a
  // click target you cannot judge -- and at least one however narrow the strip,
  // because a strip showing none of them is worse than one showing a cramped
  // one. Widths come from *all* the titles, so they do not shift as the window
  // moves across the strip.
  const auto fits = std::max<std::size_t>(1, static_cast<std::size_t>(strip.w / width));
  const std::size_t active = std::min(activeTab, titles.size() - 1);
  const std::size_t first = active < fits ? 0 : active - fits + 1;

  float x = strip.x;
  for(std::size_t i = 0; i < titles.size(); ++i) {
    TabSlot slot;
    slot.index = i;
    if(i < first) {
      // Scrolled off to the left. Given a rect outside the strip rather than a
      // plausible one, so a hit test that forgets to check `visible` misses
      // instead of quietly matching a tab nobody can see.
      slot.rect = {strip.x - width, strip.y, width, strip.h};
      slot.close = {slot.rect.x, strip.y, 0.0f, 0.0f};
      slots.push_back(slot);
      continue;
    }
    slot.rect = {x, strip.y, width, strip.h};
    // The first tab of the window is always shown, even in a strip too narrow
    // to hold a whole one: `fits` already decided to show it, and a strip that
    // then draws none of them looks broken rather than tight. The rest are by
    // fit, so visibility still never comes back once it stops.
    slot.visible = i == first || x + width <= strip.x + strip.w + 0.5f;
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
