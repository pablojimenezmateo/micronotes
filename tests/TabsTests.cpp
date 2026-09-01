#include "TestSupport.h"

#include "ui/Tabs.h"

#include <string>
#include <vector>

using micronotes::ui::layoutTabs;
using micronotes::ui::Rect;
using micronotes::ui::tabCloseHitRect;
using micronotes::ui::TabSlot;

namespace {

const Rect kStrip {0.0f, 0.0f, 900.0f, 34.0f};

// A fixed-width measurer, so every assertion below is exact arithmetic rather
// than whatever the installed fonts happen to do.
int measure(std::string_view value) {
  return static_cast<int>(value.size()) * 7;
}

std::vector<std::string> titles(std::size_t count) {
  std::vector<std::string> out;
  for(std::size_t i = 0; i < count; ++i) out.push_back("Note " + std::to_string(i));
  return out;
}

bool inside(const Rect& outer, const Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}

}

MICRONOTES_TEST(tabs_lay_out_left_to_right_without_a_gap) {
  const auto slots = layoutTabs(titles(4), kStrip, measure);
  MICRONOTES_REQUIRE(slots.size() == 4);
  MICRONOTES_REQUIRE(slots[0].rect.x == kStrip.x);
  for(std::size_t i = 1; i < slots.size(); ++i) {
    MICRONOTES_REQUIRE(slots[i].rect.x == slots[i - 1].rect.x + slots[i - 1].rect.w);
    MICRONOTES_REQUIRE(slots[i].index == i);
  }
}

// Equal widths, as a browser's are: a strip whose tabs resize as you move
// across it moves the one you were about to click.
MICRONOTES_TEST(tabs_are_all_the_same_width) {
  const auto slots = layoutTabs({"a", "a much longer title than the others", "b"}, kStrip, measure);
  MICRONOTES_REQUIRE(slots.size() == 3);
  MICRONOTES_REQUIRE(slots[0].rect.w == slots[1].rect.w);
  MICRONOTES_REQUIRE(slots[1].rect.w == slots[2].rect.w);
}

MICRONOTES_TEST(tabs_stay_within_the_width_they_are_allowed) {
  // Two tabs must not stretch halfway across a wide window.
  const auto few = layoutTabs(titles(2), kStrip, measure);
  for(const auto& slot : few) MICRONOTES_REQUIRE(slot.rect.w <= micronotes::ui::kMaxTabWidth);

  // Many tabs must still show enough of a title to tell them apart.
  const auto many = layoutTabs(titles(20), kStrip, measure);
  for(const auto& slot : many) MICRONOTES_REQUIRE(slot.rect.w >= micronotes::ui::kMinTabWidth);
}

// Once the strip is full the rest are still laid out -- scrolling is then a
// question of which slots get drawn, not a second layout that could disagree.
MICRONOTES_TEST(tabs_past_the_end_of_the_strip_are_marked_not_visible) {
  const auto slots = layoutTabs(titles(20), kStrip, measure);
  MICRONOTES_REQUIRE(slots.size() == 20);
  MICRONOTES_REQUIRE(slots.front().visible);
  MICRONOTES_REQUIRE(!slots.back().visible);
  bool seenHidden = false;
  for(const auto& slot : slots) {
    if(!slot.visible) seenHidden = true;
    // Visibility never comes back: the strip does not have holes in it.
    MICRONOTES_REQUIRE(!(seenHidden && slot.visible));
  }
}

MICRONOTES_TEST(tabs_close_button_sits_inside_its_own_tab) {
  const auto slots = layoutTabs(titles(5), kStrip, measure);
  for(const auto& slot : slots) {
    MICRONOTES_REQUIRE(inside(slot.rect, slot.close));
  }
}

// The grab area is bigger than the cross drawn in it, but not so big that it
// reaches into the neighbouring tab and closes the wrong note.
MICRONOTES_TEST(tabs_close_hit_area_is_generous_without_overlapping_a_neighbour) {
  const auto slots = layoutTabs(titles(6), kStrip, measure);
  for(std::size_t i = 0; i < slots.size(); ++i) {
    const Rect hit = tabCloseHitRect(slots[i]);
    MICRONOTES_REQUIRE(hit.w > slots[i].close.w);
    MICRONOTES_REQUIRE(hit.h > slots[i].close.h);
    MICRONOTES_REQUIRE(hit.x >= slots[i].rect.x);
    if(i + 1 < slots.size()) {
      MICRONOTES_REQUIRE(hit.x + hit.w <= slots[i + 1].rect.x);
    }
  }
}

MICRONOTES_TEST(tabs_handle_the_empty_and_degenerate_cases) {
  MICRONOTES_REQUIRE(layoutTabs({}, kStrip, measure).empty());
  MICRONOTES_REQUIRE(layoutTabs(titles(3), {0, 0, 0, 34}, measure).empty());
  // No measurer at all still produces a usable strip.
  const auto slots = layoutTabs(titles(3), kStrip, nullptr);
  MICRONOTES_REQUIRE(slots.size() == 3);
  MICRONOTES_REQUIRE(slots[0].rect.w >= micronotes::ui::kMinTabWidth);
}

// The geometry is a pure function of its inputs, which is what lets the paint
// and the hit test share it.
MICRONOTES_TEST(tabs_layout_is_the_same_every_time) {
  const auto first = layoutTabs(titles(7), kStrip, measure);
  const auto second = layoutTabs(titles(7), kStrip, measure);
  MICRONOTES_REQUIRE(first.size() == second.size());
  for(std::size_t i = 0; i < first.size(); ++i) {
    MICRONOTES_REQUIRE(first[i].rect == second[i].rect);
    MICRONOTES_REQUIRE(first[i].close == second[i].close);
  }
}
