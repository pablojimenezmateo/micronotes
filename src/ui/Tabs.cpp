#include "ui/Tabs.h"

#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {

TabStripLayout layoutTabs(const std::vector<std::string>& titles, Rect strip,
                          const std::function<int(std::string_view)>& measure,
                          std::size_t activeTab) {
  TabStripLayout layout;
  if(titles.empty() || strip.w <= 0.0f) return layout;

  // Each tab is as wide as its own title asks for, clamped.
  //
  // It used to be one width for every tab, taken from the widest title or from
  // an even share of the strip -- whichever was smaller -- which meant a single
  // long title stretched every tab beside it, and opening a note could resize
  // every tab on screen. Per-tab widths are what a browser and an IDE both do.
  std::vector<float> widths;
  widths.reserve(titles.size());
  for(const auto& title : titles) {
    const float measured =
      measure ? static_cast<float>(measure(title)) + kTabPadding : kMinTabWidth;
    widths.push_back(std::clamp(measured, kMinTabWidth, kMaxTabWidth));
  }

  // The window of tabs to show, ending at the active one. Whole tabs only --
  // half a tab at the edge is a click target you cannot judge -- and at least
  // one however narrow the strip, because a strip showing none of them is worse
  // than one showing a cramped one.
  //
  // Derived rather than stored, so it needs no state to keep in step and cannot
  // drift between the draw and the hit test.
  const std::size_t active = std::min(activeTab, titles.size() - 1);
  const bool overflows = [&] {
    float total = 0.0f;
    for(const float width : widths) total += width;
    return total > strip.w;
  }();
  // An overflowing strip keeps a chevron at each end, and they come off the
  // room the tabs have rather than sitting on top of the outermost one.
  const float reserve = overflows ? kTabScrollButtonWidth : 0.0f;
  const float room = std::max(kMinTabWidth, strip.w - reserve * 2.0f);

  std::size_t first = active;
  float used = widths[active];
  while(first > 0 && used + widths[first - 1] <= room) {
    --first;
    used += widths[first];
  }

  float x = strip.x + reserve;
  for(std::size_t i = 0; i < titles.size(); ++i) {
    TabSlot slot;
    slot.index = i;
    const float width = widths[i];
    if(i < first) {
      // Scrolled off to the left. Given a rect outside the strip rather than a
      // plausible one, so a hit test that forgets to check `visible` misses
      // instead of quietly matching a tab nobody can see.
      slot.rect = {strip.x - width, strip.y, width, strip.h};
      slot.close = {slot.rect.x, strip.y, 0.0f, 0.0f};
      layout.slots.push_back(slot);
      ++layout.hiddenLeft;
      continue;
    }
    slot.rect = {x, strip.y, width, strip.h};
    // The first tab of the window is always shown, even in a strip too narrow
    // to hold a whole one: the window already decided to show it, and a strip
    // that then draws none of them looks broken rather than tight. The rest are
    // by fit, so visibility still never comes back once it stops.
    slot.visible = i == first || x + width <= strip.x + strip.w - reserve + 0.5f;
    if(!slot.visible) ++layout.hiddenRight;
    slot.close = {
      x + width - kTabClosePadding - kTabCloseSize,
      std::round(strip.y + (strip.h - kTabCloseSize) / 2.0f),
      kTabCloseSize,
      kTabCloseSize,
    };
    layout.slots.push_back(slot);
    x += width;
  }

  if(layout.hiddenLeft > 0) {
    layout.scrollLeft = {strip.x, strip.y, kTabScrollButtonWidth, strip.h};
  }
  if(layout.hiddenRight > 0) {
    layout.scrollRight = {strip.x + strip.w - kTabScrollButtonWidth, strip.y,
                          kTabScrollButtonWidth, strip.h};
  }
  return layout;
}

float draggedTabX(Rect strip, float tabWidth, float pointerX, float grabOffsetX) {
  const float furthest = std::max(strip.x, strip.x + strip.w - tabWidth);
  return std::clamp(pointerX - grabOffsetX, strip.x, furthest);
}

std::size_t tabDropSlot(const TabStripLayout& layout, Rect strip, float x,
                        std::size_t tabCount) {
  if(tabCount == 0) return 0;
  // The first visible tab whose resting midpoint is right of `x`: the dragged
  // tab has passed everything before it, so the gap in front of that tab is
  // where it lands.
  std::size_t lastVisible = 0;
  bool sawOne = false;
  for(const auto& slot : layout.slots) {
    if(!slot.visible) continue;
    if(x < slot.rect.x + slot.rect.w / 2.0f) return slot.index;
    lastVisible = slot.index;
    sawOne = true;
  }
  // Nothing visible at all -- a strip with no room for even one tab -- leaves
  // the halves of the strip as the only answer there is.
  if(!sawOne) return x <= strip.x + strip.w / 2.0f ? 0 : tabCount;
  return std::min(tabCount, lastVisible + 1);
}

std::size_t tabIndexForDropSlot(std::size_t slot, std::size_t from, std::size_t tabCount) {
  if(tabCount == 0) return 0;
  const std::size_t clamped = std::min(slot, tabCount);
  return std::min(clamped > from ? clamped - 1 : clamped, tabCount - 1);
}

bool moveTab(std::vector<NoteTab>& tabs, std::size_t& activeTab, std::size_t from,
             std::size_t to) {
  if(from >= tabs.size() || to >= tabs.size()) return false;
  if(from == to) return true;
  const auto begin = tabs.begin();
  const auto at = [&](std::size_t i) { return begin + static_cast<std::ptrdiff_t>(i); };
  if(from < to) std::rotate(at(from), at(from + 1), at(to + 1));
  else std::rotate(at(to), at(from), at(from + 1));
  // The tab showing must still be showing afterwards, and it is not
  // necessarily the one that moved: the strip's menu reorders the tab it was
  // opened on. Which way the active index shifts is decided by whether the
  // moved tab passed over it.
  if(activeTab == from) activeTab = to;
  else if(from < to && activeTab > from && activeTab <= to) --activeTab;
  else if(to < from && activeTab >= to && activeTab < from) ++activeTab;
  return true;
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
