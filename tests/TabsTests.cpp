#include "TestSupport.h"

#include "ui/Tabs.h"

#include "ui/Metrics.h"

#include <algorithm>
#include <string>
#include <utility>
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

// The tests below assert about the tabs, not about the strip's furniture, so
// they take the slots and leave the chevrons to the two tests that check them.
std::vector<TabSlot> slotsOf(const micronotes::ui::TabStripLayout& layout) {
  return layout.slots;
}

bool inside(const Rect& outer, const Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}

}

MICRONOTES_TEST(tabs_lay_out_left_to_right_without_a_gap) {
  const auto slots = slotsOf(layoutTabs(titles(4), kStrip, measure));
  MICRONOTES_REQUIRE(slots.size() == 4);
  // Four short tabs fit, so the strip reserves no room for a chevron and the
  // first tab starts at its leading edge.
  MICRONOTES_REQUIRE(slots[0].rect.x == kStrip.x);
  for(std::size_t i = 1; i < slots.size(); ++i) {
    MICRONOTES_REQUIRE(slots[i].rect.x == slots[i - 1].rect.x + slots[i - 1].rect.w);
    MICRONOTES_REQUIRE(slots[i].index == i);
  }
}

// Each tab is as wide as its own title asks for, clamped.
//
// It used to be one width for every tab, taken from the widest title or from an
// even share of the strip -- whichever was smaller. That meant a single long
// title stretched every tab beside it, and opening a note with a long name
// resized every tab on screen, which is the thing a fixed width was supposed to
// prevent. A browser and an IDE both size per tab and clamp.
MICRONOTES_TEST(tabs_are_as_wide_as_their_own_titles_ask) {
  const auto slots =
    slotsOf(layoutTabs({"a", "a much longer title than the others", "b"}, kStrip, measure));
  MICRONOTES_REQUIRE(slots.size() == 3);
  MICRONOTES_REQUIRE(slots[1].rect.w > slots[0].rect.w);
  // Two equally short titles get equally narrow tabs, at the floor.
  MICRONOTES_REQUIRE(slots[0].rect.w == slots[2].rect.w);
  MICRONOTES_REQUIRE(slots[0].rect.w == micronotes::ui::kMinTabWidth);
  // And the long one is capped rather than allowed to eat the strip.
  MICRONOTES_REQUIRE(slots[1].rect.w == micronotes::ui::kMaxTabWidth);
}

MICRONOTES_TEST(tabs_stay_within_the_width_they_are_allowed) {
  // Two tabs must not stretch halfway across a wide window.
  const auto few = slotsOf(layoutTabs(titles(2), kStrip, measure));
  for(const auto& slot : few) MICRONOTES_REQUIRE(slot.rect.w <= micronotes::ui::kMaxTabWidth);

  // Many tabs must still show enough of a title to tell them apart.
  const auto many = slotsOf(layoutTabs(titles(20), kStrip, measure));
  for(const auto& slot : many) MICRONOTES_REQUIRE(slot.rect.w >= micronotes::ui::kMinTabWidth);
}

// Once the strip is full the rest are still laid out -- scrolling is then a
// question of which slots get drawn, not a second layout that could disagree.
MICRONOTES_TEST(tabs_past_the_end_of_the_strip_are_marked_not_visible) {
  const auto slots = slotsOf(layoutTabs(titles(20), kStrip, measure));
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
  const auto slots = slotsOf(layoutTabs(titles(5), kStrip, measure));
  for(const auto& slot : slots) {
    MICRONOTES_REQUIRE(inside(slot.rect, slot.close));
  }
}

// The grab area is bigger than the cross drawn in it, but not so big that it
// reaches into the neighbouring tab and closes the wrong note.
MICRONOTES_TEST(tabs_close_hit_area_is_generous_without_overlapping_a_neighbour) {
  const auto slots = slotsOf(layoutTabs(titles(6), kStrip, measure));
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
  MICRONOTES_REQUIRE(slotsOf(layoutTabs({}, kStrip, measure)).empty());
  MICRONOTES_REQUIRE(slotsOf(layoutTabs(titles(3), {0, 0, 0, 34}, measure)).empty());
  // No measurer at all still produces a usable strip.
  const auto slots = slotsOf(layoutTabs(titles(3), kStrip, nullptr));
  MICRONOTES_REQUIRE(slots.size() == 3);
  MICRONOTES_REQUIRE(slots[0].rect.w >= micronotes::ui::kMinTabWidth);
}

// The geometry is a pure function of its inputs, which is what lets the paint
// and the hit test share it.
MICRONOTES_TEST(tabs_layout_is_the_same_every_time) {
  const auto first = slotsOf(layoutTabs(titles(7), kStrip, measure));
  const auto second = slotsOf(layoutTabs(titles(7), kStrip, measure));
  MICRONOTES_REQUIRE(first.size() == second.size());
  for(std::size_t i = 0; i < first.size(); ++i) {
    MICRONOTES_REQUIRE(first[i].rect == second[i].rect);
    MICRONOTES_REQUIRE(first[i].close == second[i].close);
  }
}

// The strip's geometry is *not* independent of the measurer, and a hit test
// that laid it out without one tested rects that were not on screen.
//
// `handleTabStripClick` passed nullptr on a comment saying the geometry was a
// pure function of the titles and the strip. It is not: `layoutTabs` narrows
// every tab to the widest title when that is less than an even share, which is
// the ordinary case -- a couple of short note names in a wide strip. The drawn
// tabs were a fraction of the width the hit test believed, so a click on the
// second tab landed inside the first one's rect and activated the wrong note,
// a click past the last drawn tab still hit one, and the close cross's target
// sat in empty strip to the right of the cross. The strip read as inert.
//
// So this pins the disagreement itself rather than the fix: a title long enough
// to want more than the floor must lay out differently with and without a
// measurer, which is what makes passing the right one load-bearing.
MICRONOTES_TEST(tabs_need_the_measurer_the_draw_used) {
  const std::vector<std::string> two {"A note with a good long name on it", "Ideas"};
  const auto measured = slotsOf(layoutTabs(two, kStrip, measure));
  const auto unmeasured = slotsOf(layoutTabs(two, kStrip, {}));
  MICRONOTES_REQUIRE(measured.size() == unmeasured.size());
  // Without a measurer every tab falls back to the floor, so the first tab is
  // narrower than it was drawn and the second one starts in the wrong place.
  MICRONOTES_REQUIRE(measured[0].rect.w != unmeasured[0].rect.w);
  MICRONOTES_REQUIRE(measured[1].rect.x != unmeasured[1].rect.x);

  // And the disagreement is the one that misroutes a click. A point inside the
  // *first* tab as drawn falls inside the second tab's rect as a measurer-less
  // hit test sees it, which is exactly the wrong note being activated.
  const float insideDrawnFirst = unmeasured[0].rect.x + unmeasured[0].rect.w + 4.0f;
  MICRONOTES_REQUIRE(insideDrawnFirst < measured[0].rect.x + measured[0].rect.w);
  MICRONOTES_REQUIRE(insideDrawnFirst > unmeasured[1].rect.x);
  MICRONOTES_REQUIRE(insideDrawnFirst < unmeasured[1].rect.x + unmeasured[1].rect.w);

  // The close target moves with the tab's right edge, so it came adrift too.
  MICRONOTES_REQUIRE(tabCloseHitRect(measured[1]).x != tabCloseHitRect(unmeasured[1]).x);
}

// The property the fix relies on: laid out through one measurer, a click at the
// centre of every drawn tab resolves to that tab and no other.
MICRONOTES_TEST(tabs_hit_test_agrees_with_the_layout_it_was_drawn_from) {
  for(const std::size_t count : {1u, 2u, 3u, 7u, 20u}) {
    const auto slots = slotsOf(layoutTabs(titles(count), kStrip, measure));
    for(const auto& slot : slots) {
      if(!slot.visible) continue;
      const float x = slot.rect.x + slot.rect.w / 2.0f;
      const float y = slot.rect.y + slot.rect.h / 2.0f;
      std::size_t hit = slots.size();
      for(const auto& candidate : slots) {
        if(!candidate.visible) continue;
        if(x < candidate.rect.x || x >= candidate.rect.x + candidate.rect.w) continue;
        if(y < candidate.rect.y || y >= candidate.rect.y + candidate.rect.h) continue;
        hit = candidate.index;
        break;
      }
      MICRONOTES_REQUIRE(hit == slot.index);
    }
  }
}

// Every note opening in its own tab means the strip routinely holds more tabs
// than fit, so a tab past the right edge has to be reachable.
//
// It was not. Slots past the edge were laid out and then simply not drawn, on a
// comment saying that scrolling "is a matter of which slots are drawn rather
// than a second layout" -- which was the right plan and was never finished. So
// with more than about ten notes open, the newest tab existed, was active, and
// could not be seen or clicked; only Ctrl+Tab reached it.
//
// The window is derived from the active tab rather than stored, so there is no
// scroll state for the draw and the hit test to disagree about.
MICRONOTES_TEST(tabs_scroll_to_keep_the_active_tab_on_screen) {
  const auto names = titles(30);
  const auto visibleIndices = [](const std::vector<TabSlot>& slots) {
    std::vector<std::size_t> out;
    for(const auto& slot : slots) {
      if(slot.visible) out.push_back(slot.index);
    }
    return out;
  };

  // Whichever tab is active, it is on screen.
  for(std::size_t active = 0; active < names.size(); ++active) {
    const auto slots = slotsOf(layoutTabs(names, kStrip, measure, active));
    MICRONOTES_REQUIRE(slots.size() == names.size());
    MICRONOTES_REQUIRE(slots[active].visible);

    const auto shown = visibleIndices(slots);
    MICRONOTES_REQUIRE(!shown.empty());
    // A contiguous run, in order: the strip is a window over the tabs, never a
    // set of them with holes.
    for(std::size_t i = 1; i < shown.size(); ++i) {
      MICRONOTES_REQUIRE(shown[i] == shown[i - 1] + 1);
    }
    // Every visible tab sits inside the strip, and they tile it left to right
    // starting at its left edge.
    // An overflowing strip keeps a chevron at each end, so the window of tabs
    // starts one button in rather than at the strip's own edge.
    MICRONOTES_REQUIRE(slots[shown.front()].rect.x ==
                       kStrip.x + micronotes::ui::kTabScrollButtonWidth);
    for(const auto index : shown) {
      const Rect box = slots[index].rect;
      MICRONOTES_REQUIRE(box.x >= kStrip.x);
      MICRONOTES_REQUIRE(box.x + box.w <=
                         kStrip.x + kStrip.w - micronotes::ui::kTabScrollButtonWidth + 0.5f);
      // And its close button came with it, inside its own tab.
      MICRONOTES_REQUIRE(inside(box, slots[index].close));
    }
  }

  // A tab scrolled off to the left is not merely undrawn: its rect is outside
  // the strip, so a hit test that forgot to check `visible` misses it rather
  // than quietly matching a tab nobody can see.
  const auto slots = slotsOf(layoutTabs(names, kStrip, measure, names.size() - 1));
  MICRONOTES_REQUIRE(!slots.front().visible);
  MICRONOTES_REQUIRE(slots.front().rect.x + slots.front().rect.w <= kStrip.x);
}

// An overflowing strip says so at both ends, with the count of what is hidden
// there. A strip that fits grows no furniture at all -- two notes open should
// not look like a strip that has been scrolled.
MICRONOTES_TEST(tabs_grow_overflow_chevrons_only_when_they_overflow) {
  const auto fits = layoutTabs(titles(4), kStrip, measure);
  MICRONOTES_REQUIRE(fits.hiddenLeft == 0);
  MICRONOTES_REQUIRE(fits.hiddenRight == 0);
  MICRONOTES_REQUIRE(micronotes::ui::empty(fits.scrollLeft));
  MICRONOTES_REQUIRE(micronotes::ui::empty(fits.scrollRight));

  // Thirty notes in a 900px strip: the active tab is the last, so everything
  // before the window is hidden to the left and the chevron says how many.
  const auto names = titles(30);
  const auto scrolled = layoutTabs(names, kStrip, measure, names.size() - 1);
  MICRONOTES_REQUIRE(scrolled.hiddenLeft > 0);
  MICRONOTES_REQUIRE(!micronotes::ui::empty(scrolled.scrollLeft));
  MICRONOTES_REQUIRE(scrolled.scrollLeft.x == kStrip.x);
  MICRONOTES_REQUIRE(scrolled.hiddenRight == 0);

  // And from the first tab, the hidden ones are all to the right.
  const auto atStart = layoutTabs(names, kStrip, measure, 0);
  MICRONOTES_REQUIRE(atStart.hiddenLeft == 0);
  MICRONOTES_REQUIRE(atStart.hiddenRight > 0);
  MICRONOTES_REQUIRE(!micronotes::ui::empty(atStart.scrollRight));
  MICRONOTES_REQUIRE(atStart.scrollRight.x + atStart.scrollRight.w == kStrip.x + kStrip.w);

  // Every tab is accounted for: shown, hidden left, or hidden right.
  std::size_t shown = 0;
  for(const auto& slot : atStart.slots) {
    if(slot.visible) ++shown;
  }
  MICRONOTES_REQUIRE(shown + atStart.hiddenLeft + atStart.hiddenRight == names.size());
}

// The narrowest strip still shows the tab you are on: one cramped tab beats
// none, and a strip showing none of them looks broken rather than tight.
MICRONOTES_TEST(tabs_show_the_active_one_however_narrow_the_strip) {
  const Rect sliver {0.0f, 0.0f, 40.0f, 34.0f};
  const auto names = titles(12);
  for(const std::size_t active : {std::size_t {0}, std::size_t {5}, std::size_t {11}}) {
    const auto slots = slotsOf(layoutTabs(names, sliver, measure, active));
    MICRONOTES_REQUIRE(slots[active].visible);
    int shown = 0;
    for(const auto& slot : slots) {
      if(slot.visible) ++shown;
    }
    MICRONOTES_REQUIRE(shown == 1);
  }
}

// An out-of-range active index must not walk off the end of the title list.
MICRONOTES_TEST(tabs_survive_an_active_index_past_the_end) {
  const auto slots = slotsOf(layoutTabs(titles(3), kStrip, measure, 99));
  MICRONOTES_REQUIRE(slots.size() == 3);
  MICRONOTES_REQUIRE(slots.back().visible);
}

// --- dragging a tab into a new place ---------------------------------------

namespace {

std::vector<micronotes::ui::NoteTab> tabsNamed(std::string_view ids) {
  std::vector<micronotes::ui::NoteTab> out;
  for(const char c : ids) out.push_back({std::string(1, c), {}, false});
  return out;
}

std::string orderOf(const std::vector<micronotes::ui::NoteTab>& tabs) {
  std::string out;
  for(const auto& tab : tabs) out += tab.noteId;
  return out;
}

}

MICRONOTES_TEST(tabs_a_move_to_the_right_slides_the_ones_it_passed_left) {
  auto tabs = tabsNamed("abcde");
  std::size_t active = 1;
  MICRONOTES_REQUIRE(micronotes::ui::moveTab(tabs, active, 1, 3));
  MICRONOTES_REQUIRE(orderOf(tabs) == "acdbe");
  // The active tab moved with the tab it named, not with the index.
  MICRONOTES_REQUIRE(active == 3);
  MICRONOTES_REQUIRE(tabs[active].noteId == "b");
}

MICRONOTES_TEST(tabs_a_move_to_the_left_slides_the_ones_it_passed_right) {
  auto tabs = tabsNamed("abcde");
  std::size_t active = 3;
  MICRONOTES_REQUIRE(micronotes::ui::moveTab(tabs, active, 3, 0));
  MICRONOTES_REQUIRE(orderOf(tabs) == "dabce");
  MICRONOTES_REQUIRE(active == 0);
  MICRONOTES_REQUIRE(tabs[active].noteId == "d");
}

// The strip's own menu reorders the tab it was opened on, which need not be the
// one showing -- so the note on screen has to stay on screen whichever side of
// it the moved tab passes.
MICRONOTES_TEST(tabs_a_move_keeps_the_note_showing_on_screen) {
  for(const auto& [from, to] : {std::pair<std::size_t, std::size_t> {0, 4},
                               std::pair<std::size_t, std::size_t> {4, 0},
                               std::pair<std::size_t, std::size_t> {1, 2},
                               std::pair<std::size_t, std::size_t> {3, 2}}) {
    for(std::size_t active = 0; active < 5; ++active) {
      auto tabs = tabsNamed("abcde");
      const std::string showing = tabs[active].noteId;
      std::size_t moved = active;
      MICRONOTES_REQUIRE(micronotes::ui::moveTab(tabs, moved, from, to));
      MICRONOTES_REQUIRE(moved < tabs.size());
      micronotes::tests::require(tabs[moved].noteId == showing,
                                 "moving " + std::to_string(from) + " to " + std::to_string(to) +
                                   " lost the note showing at " + std::to_string(active));
    }
  }
}

MICRONOTES_TEST(tabs_a_move_out_of_range_changes_nothing) {
  auto tabs = tabsNamed("abc");
  std::size_t active = 1;
  MICRONOTES_REQUIRE(!micronotes::ui::moveTab(tabs, active, 0, 9));
  MICRONOTES_REQUIRE(!micronotes::ui::moveTab(tabs, active, 9, 0));
  MICRONOTES_REQUIRE(orderOf(tabs) == "abc");
  MICRONOTES_REQUIRE(active == 1);
  // A move onto its own place is a no-op that succeeded, not a failure.
  MICRONOTES_REQUIRE(micronotes::ui::moveTab(tabs, active, 1, 1));
  MICRONOTES_REQUIRE(orderOf(tabs) == "abc");
}

// A slot is a gap and an index is a tab, and they differ by one on the right of
// the tab being carried, because the tabs it passed have slid left to fill the
// hole it left.
MICRONOTES_TEST(tabs_a_drop_slot_becomes_the_index_the_tab_lands_at) {
  using micronotes::ui::tabIndexForDropSlot;
  // Carrying tab 1 rightwards.
  MICRONOTES_REQUIRE(tabIndexForDropSlot(0, 1, 5) == 0);
  MICRONOTES_REQUIRE(tabIndexForDropSlot(1, 1, 5) == 1);
  MICRONOTES_REQUIRE(tabIndexForDropSlot(2, 1, 5) == 1);
  MICRONOTES_REQUIRE(tabIndexForDropSlot(3, 1, 5) == 2);
  // Past the last gap, which is one more than there are tabs.
  MICRONOTES_REQUIRE(tabIndexForDropSlot(5, 1, 5) == 4);
  // Carrying tab 3 leftwards: no slide, so slot and index agree.
  MICRONOTES_REQUIRE(tabIndexForDropSlot(1, 3, 5) == 1);
  MICRONOTES_REQUIRE(tabIndexForDropSlot(0, 3, 5) == 0);
  // Nothing open at all.
  MICRONOTES_REQUIRE(tabIndexForDropSlot(0, 0, 0) == 0);
}

MICRONOTES_TEST(tabs_the_drop_slot_is_the_gap_the_carried_tab_has_reached) {
  const auto names = titles(4);
  const auto layout = layoutTabs(names, kStrip, measure, 0);
  const auto& slots = layout.slots;
  MICRONOTES_REQUIRE(slots.size() == 4);
  const auto slotAt = [&](float x) {
    return micronotes::ui::tabDropSlot(layout, kStrip, x, names.size());
  };
  // Before the first tab's midpoint: the gap in front of it.
  MICRONOTES_REQUIRE(slotAt(kStrip.x) == 0);
  MICRONOTES_REQUIRE(slotAt(slots[0].rect.x + slots[0].rect.w / 2.0f - 1.0f) == 0);
  // Past it: the gap in front of the second.
  MICRONOTES_REQUIRE(slotAt(slots[0].rect.x + slots[0].rect.w / 2.0f + 1.0f) == 1);
  MICRONOTES_REQUIRE(slotAt(slots[2].rect.x + slots[2].rect.w / 2.0f + 1.0f) == 3);
  // Past the last tab's midpoint: the gap after every tab there is.
  MICRONOTES_REQUIRE(slotAt(slots[3].rect.x + slots[3].rect.w) == 4);
  // And with nothing open there is one gap, whatever the pointer says.
  MICRONOTES_REQUIRE(micronotes::ui::tabDropSlot(layout, kStrip, 500.0f, 0) == 0);
}

// A scrolled strip is a window onto the list, so a drop past a visible edge
// pins to what is on screen rather than teleporting the tab to a slot nobody
// can see. The chevrons are how the window moves.
MICRONOTES_TEST(tabs_a_drop_past_a_visible_edge_pins_to_what_is_on_screen) {
  const auto names = titles(20);
  const Rect narrow {0.0f, 0.0f, 420.0f, 34.0f};
  const auto layout = layoutTabs(names, narrow, measure, 10);
  std::size_t firstVisible = names.size();
  std::size_t lastVisible = 0;
  for(const auto& slot : layout.slots) {
    if(!slot.visible) continue;
    firstVisible = std::min(firstVisible, slot.index);
    lastVisible = std::max(lastVisible, slot.index);
  }
  MICRONOTES_REQUIRE(firstVisible > 0);
  MICRONOTES_REQUIRE(lastVisible + 1 < names.size());
  MICRONOTES_REQUIRE(micronotes::ui::tabDropSlot(layout, narrow, narrow.x - 200.0f,
                                                 names.size()) == firstVisible);
  MICRONOTES_REQUIRE(micronotes::ui::tabDropSlot(layout, narrow, narrow.x + narrow.w + 200.0f,
                                                 names.size()) == lastVisible + 1);
}

// The pointer keeps the grip it took on the tab, and the tab stays inside the
// strip -- including when the strip is narrower than the tab, where clamping
// the other way round would be undefined behaviour.
MICRONOTES_TEST(tabs_the_carried_tab_keeps_its_grip_and_stays_in_the_strip) {
  using micronotes::ui::draggedTabX;
  MICRONOTES_REQUIRE(draggedTabX(kStrip, 132.0f, 400.0f, 30.0f) == 370.0f);
  // Dragged off the left edge and off the right.
  MICRONOTES_REQUIRE(draggedTabX(kStrip, 132.0f, -500.0f, 30.0f) == kStrip.x);
  MICRONOTES_REQUIRE(draggedTabX(kStrip, 132.0f, 5000.0f, 30.0f) ==
                     kStrip.x + kStrip.w - 132.0f);
  // A strip with no room for the tab at all pins it to the leading edge.
  const Rect sliver {10.0f, 0.0f, 40.0f, 34.0f};
  MICRONOTES_REQUIRE(draggedTabX(sliver, 132.0f, 5000.0f, 0.0f) == sliver.x);
  MICRONOTES_REQUIRE(draggedTabX(sliver, 132.0f, -5000.0f, 0.0f) == sliver.x);
}
