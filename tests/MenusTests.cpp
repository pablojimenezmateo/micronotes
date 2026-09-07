#include "TestSupport.h"

#include "ui/Actions.h"
#include "ui/Menus.h"
#include "ui/Metrics.h"

#include <algorithm>
#include <set>
#include <string>

using micronotes::ui::ActionId;
using micronotes::ui::MenuBarLayout;
using micronotes::ui::MenuId;
using micronotes::ui::MenuItemSpec;
using micronotes::ui::menuBarLayout;
using micronotes::ui::menuPopupItemAt;
using micronotes::ui::menuPopupItemRect;
using micronotes::ui::menuPopupRect;
using micronotes::ui::menuSpecs;
using micronotes::ui::Rect;

namespace {

// A stand-in for the mono chrome face: every glyph the same width, which is
// what a mono face is, so the widths a test asserts on are arithmetic rather
// than whatever the machine happened to have installed.
int measure(std::string_view value) {
  return static_cast<int>(value.size()) * 8;
}

Rect wideBar() {
  return {0.0f, 0.0f, 1600.0f, micronotes::ui::kMenuBarHeight};
}

Rect window() {
  return {0.0f, 0.0f, 1600.0f, 900.0f};
}

bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

}

// The bar lays out left to right, in table order, with nothing overlapping and
// nothing outside the strip it was given.
MICRONOTES_TEST(menu_bar_lays_out_left_to_right_without_overlapping) {
  const MenuBarLayout layout = menuBarLayout(wideBar(), MenuId::None, false, measure);
  MICRONOTES_REQUIRE(layout.items.size() == menuSpecs().size());
  MICRONOTES_REQUIRE(layout.overflow.empty());
  MICRONOTES_REQUIRE(micronotes::ui::empty(layout.chevron));

  for(std::size_t i = 0; i < layout.items.size(); ++i) {
    MICRONOTES_REQUIRE(layout.items[i].id == menuSpecs()[i].id);
    MICRONOTES_REQUIRE(layout.items[i].rect.w >= micronotes::ui::kMenuBarItemMinWidth);
    MICRONOTES_REQUIRE(layout.items[i].rect.w <= micronotes::ui::kMenuBarItemMaxWidth);
    MICRONOTES_REQUIRE(layout.items[i].rect.y >= wideBar().y);
    MICRONOTES_REQUIRE(layout.items[i].rect.y + layout.items[i].rect.h <=
                       wideBar().y + wideBar().h + 0.001f);
    if(i == 0) continue;
    const Rect previous = layout.items[i - 1].rect;
    MICRONOTES_REQUIRE(layout.items[i].rect.x >= previous.x + previous.w);
    MICRONOTES_REQUIRE(!overlaps(previous, layout.items[i].rect));
  }
}

// Only the open menu is marked active, and only when it is actually on the bar.
MICRONOTES_TEST(menu_bar_marks_the_open_menu_and_only_that_one) {
  const MenuBarLayout layout = menuBarLayout(wideBar(), MenuId::View, false, measure);
  int active = 0;
  for(const auto& item : layout.items) {
    if(!item.active) continue;
    ++active;
    MICRONOTES_REQUIRE(item.id == MenuId::View);
  }
  MICRONOTES_REQUIRE(active == 1);
}

// A bar too narrow for every menu drops the ones that did not fit into the
// overflow list and grows a chevron for them. Dropped, not squeezed: a menu
// narrowed until its label is an ellipsis is a menu nobody can use.
MICRONOTES_TEST(menu_bar_overflows_into_a_chevron_when_the_window_is_narrow) {
  const Rect narrow {0.0f, 0.0f, 220.0f, micronotes::ui::kMenuBarHeight};
  const MenuBarLayout layout = menuBarLayout(narrow, MenuId::None, false, measure);
  MICRONOTES_REQUIRE(layout.items.size() < menuSpecs().size());
  MICRONOTES_REQUIRE(!layout.overflow.empty());
  MICRONOTES_REQUIRE(!micronotes::ui::empty(layout.chevron));
  MICRONOTES_REQUIRE(layout.items.size() + layout.overflow.size() == menuSpecs().size());
  // Nothing placed runs into the chevron, and the chevron stays in the strip.
  for(const auto& item : layout.items) {
    MICRONOTES_REQUIRE(!overlaps(item.rect, layout.chevron));
  }
  MICRONOTES_REQUIRE(layout.chevron.x + layout.chevron.w <= narrow.x + narrow.w + 0.001f);
  // The overflow is the tail of the table, so a menu never changes places.
  for(std::size_t i = 0; i < layout.overflow.size(); ++i) {
    MICRONOTES_REQUIRE(layout.overflow[i] == menuSpecs()[layout.items.size() + i].id);
  }
}

// A bar that exactly fits must not spuriously overflow. The trailing gap after
// the last item is not real -- n items have n-1 gaps between them -- and
// counting it is how a bar one pixel wide enough grows a chevron for nothing.
MICRONOTES_TEST(menu_bar_that_exactly_fits_does_not_overflow) {
  // Walk widths down from roomy until the bar first overflows, and check the
  // width just above that fits every menu with no chevron.
  float lastFitting = 0.0f;
  for(float width = 1600.0f; width > 120.0f; width -= 1.0f) {
    const MenuBarLayout layout =
      menuBarLayout({0.0f, 0.0f, width, micronotes::ui::kMenuBarHeight}, MenuId::None, false,
                    measure);
    if(layout.overflow.empty()) lastFitting = width;
    else break;
  }
  MICRONOTES_REQUIRE(lastFitting > 0.0f);
  const MenuBarLayout tight =
    menuBarLayout({0.0f, 0.0f, lastFitting, micronotes::ui::kMenuBarHeight}, MenuId::None, false,
                  measure);
  MICRONOTES_REQUIRE(tight.items.size() == menuSpecs().size());
  MICRONOTES_REQUIRE(micronotes::ui::empty(tight.chevron));
  // And the last item is inside the bar, which is what "fits" has to mean.
  const Rect last = tight.items.back().rect;
  MICRONOTES_REQUIRE(last.x + last.w <= lastFitting + 0.001f);
}

// The window controls take room from the menus rather than sitting on top of
// them, and they are only there when this window draws its own frame.
MICRONOTES_TEST(menu_bar_reserves_room_for_the_window_controls) {
  const MenuBarLayout without = menuBarLayout(wideBar(), MenuId::None, false, measure);
  for(const Rect& button : without.windowButtons) {
    MICRONOTES_REQUIRE(micronotes::ui::empty(button));
  }

  const MenuBarLayout with = menuBarLayout(wideBar(), MenuId::None, true, measure);
  for(std::size_t i = 0; i < with.windowButtons.size(); ++i) {
    const Rect button = with.windowButtons[i];
    MICRONOTES_REQUIRE(!micronotes::ui::empty(button));
    MICRONOTES_REQUIRE(button.x + button.w <= wideBar().w + 0.001f);
    for(const auto& item : with.items) MICRONOTES_REQUIRE(!overlaps(item.rect, button));
    if(i == 0) continue;
    // Left to right, so close ends nearest the corner.
    MICRONOTES_REQUIRE(button.x > with.windowButtons[i - 1].x);
  }
  // Close is the last one, and it ends within an inset of the edge -- a control
  // inset from the corner is one the pointer cannot be thrown at.
  const Rect close = with.windowButtons.back();
  MICRONOTES_REQUIRE(close.x + close.w >=
                     wideBar().w - micronotes::ui::kWindowButtonRightInset - 0.001f);
}

// The popup hangs under its anchor, stays inside the window, and is tall enough
// for every row it holds.
MICRONOTES_TEST(menu_popup_hangs_under_its_anchor_inside_the_window) {
  const auto* file = micronotes::ui::findMenu(MenuId::File);
  MICRONOTES_REQUIRE(file != nullptr);
  const Rect anchor {8.0f, 3.0f, 60.0f, 19.0f};
  const Rect popup = menuPopupRect(anchor, file->items, window(), measure);

  MICRONOTES_REQUIRE(popup.w >= micronotes::ui::kMenuPopupMinWidth);
  MICRONOTES_REQUIRE(popup.y >= anchor.y);
  MICRONOTES_REQUIRE(popup.x >= window().x);
  MICRONOTES_REQUIRE(popup.x + popup.w <= window().x + window().w + 0.001f);
  MICRONOTES_REQUIRE(popup.y + popup.h <= window().y + window().h + 0.001f);

  // Every row is inside it, in order, and nothing overlaps.
  for(std::size_t i = 0; i < file->items.size(); ++i) {
    const Rect row = menuPopupItemRect(popup, file->items, i);
    MICRONOTES_REQUIRE(row.y >= popup.y);
    MICRONOTES_REQUIRE(row.y + row.h <= popup.y + popup.h + 0.001f);
    if(i == 0) continue;
    const Rect previous = menuPopupItemRect(popup, file->items, i - 1);
    MICRONOTES_REQUIRE(row.y >= previous.y + previous.h - 0.001f);
  }
}

// A popup that would fall off the bottom flips above its anchor instead of
// being clipped or scrolled.
MICRONOTES_TEST(menu_popup_flips_above_an_anchor_near_the_bottom) {
  const auto* go = micronotes::ui::findMenu(MenuId::Go);
  MICRONOTES_REQUIRE(go != nullptr);
  const Rect shortWindow {0.0f, 0.0f, 1600.0f, 300.0f};
  const Rect low {8.0f, 260.0f, 60.0f, 19.0f};
  const Rect flipped = menuPopupRect(low, go->items, shortWindow, measure);
  MICRONOTES_REQUIRE(flipped.y + flipped.h <= shortWindow.h + 0.001f);
  MICRONOTES_REQUIRE(flipped.y >= 0.0f);
  // Actually flipped, not merely nudged: it ends at or above the anchor's top.
  MICRONOTES_REQUIRE(flipped.y + flipped.h <= low.y + low.h + 0.001f);

  // The same menu off a bar at the top hangs down, so the flip is a response to
  // the anchor rather than something the popup always does.
  const Rect high = menuPopupRect({8.0f, 3.0f, 60.0f, 19.0f}, go->items, shortWindow, measure);
  MICRONOTES_REQUIRE(high.y >= 3.0f);
}

// A menu taller than the window it opens in is capped rather than allowed to
// hang off the bottom, where its items would be neither visible nor reasonable
// about. It costs the tail of the longest menu on a very short window.
MICRONOTES_TEST(menu_popup_never_leaves_the_window_however_tall_the_menu) {
  for(const auto& menu : menuSpecs()) {
    for(const float height : {160.0f, 300.0f, 480.0f, 900.0f}) {
      const Rect bounds {0.0f, 0.0f, 1600.0f, height};
      for(const float anchorY : {3.0f, height * 0.5f, height - 24.0f}) {
        const Rect popup =
          menuPopupRect({8.0f, anchorY, 60.0f, 19.0f}, menu.items, bounds, measure);
        MICRONOTES_REQUIRE(popup.y >= bounds.y);
        MICRONOTES_REQUIRE(popup.y + popup.h <= bounds.y + bounds.h + 0.001f);
        MICRONOTES_REQUIRE(popup.x >= bounds.x);
        MICRONOTES_REQUIRE(popup.x + popup.w <= bounds.x + bounds.w + 0.001f);
      }
    }
  }
}

// A separator has a row of its own so the two indices are the same number, but
// it is never what a click lands on.
MICRONOTES_TEST(menu_popup_hit_test_steps_over_separators) {
  const auto* file = micronotes::ui::findMenu(MenuId::File);
  MICRONOTES_REQUIRE(file != nullptr);
  const Rect popup = menuPopupRect({8.0f, 3.0f, 60.0f, 19.0f}, file->items, window(), measure);

  bool sawSeparator = false;
  for(std::size_t i = 0; i < file->items.size(); ++i) {
    const Rect row = menuPopupItemRect(popup, file->items, i);
    const float cx = row.x + row.w / 2.0f;
    const float cy = row.y + row.h / 2.0f;
    const auto hit = menuPopupItemAt(popup, file->items, cx, cy);
    if(file->items[i].separator) {
      sawSeparator = true;
      MICRONOTES_REQUIRE(!hit.has_value());
      continue;
    }
    MICRONOTES_REQUIRE(hit.has_value());
    MICRONOTES_REQUIRE(*hit == i);
  }
  MICRONOTES_REQUIRE(sawSeparator);
  // And nothing outside the popup hits anything.
  MICRONOTES_REQUIRE(!menuPopupItemAt(popup, file->items, popup.x - 4.0f, popup.y + 8.0f));
}

// The load-bearing one. The bar is one of four views of the action registry,
// and the whole point of it is that it is the *readable* one: an action the
// palette offers but no menu lists is one a user can only reach by already
// knowing it exists.
MICRONOTES_TEST(every_palette_action_appears_in_exactly_one_menu) {
  std::set<std::string> listed;
  std::string twice;
  for(const auto& menu : menuSpecs()) {
    for(const auto& item : menu.items) {
      if(item.separator) continue;
      const auto* spec = micronotes::ui::findAction(item.action);
      MICRONOTES_REQUIRE(spec != nullptr);
      if(!listed.insert(std::string(spec->name)).second) {
        if(!twice.empty()) twice += ", ";
        twice += spec->name;
      }
    }
  }
  micronotes::tests::require(
    twice.empty(),
    "actions listed by more than one menu: " + twice +
      " -- a command in two places is a command a reader has to check twice");

  std::string missing;
  for(const auto& spec : micronotes::ui::actionSpecs()) {
    if(!spec.inPalette) continue;
    if(listed.count(std::string(spec.name)) != 0) continue;
    if(!missing.empty()) missing += ", ";
    missing += spec.name;
  }
  micronotes::tests::require(
    missing.empty(),
    "actions the palette lists but no menu does: " + missing +
      " -- the menu bar is the shell's readable surface, and a command only the "
      "palette knows about is one you have to already know to find");
}

// Every item resolves to a real action with a label to print. An item whose
// action fell out of the registry would draw an empty row.
MICRONOTES_TEST(every_menu_item_has_a_label) {
  for(const auto& menu : menuSpecs()) {
    MICRONOTES_REQUIRE(!menu.label.empty());
    MICRONOTES_REQUIRE(!menu.items.empty());
    // A menu that opens or closes on a separator would highlight nothing.
    MICRONOTES_REQUIRE(!menu.items.front().separator);
    MICRONOTES_REQUIRE(!menu.items.back().separator);
    for(const auto& item : menu.items) {
      if(item.separator) continue;
      MICRONOTES_REQUIRE(!micronotes::ui::menuItemLabel(item).empty());
    }
  }
}
