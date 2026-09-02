#include "TestSupport.h"

#include "ui/Metrics.h"
#include "ui/RibbonLayout.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using micronotes::ui::ActionId;
using micronotes::ui::Rect;
using micronotes::ui::ribbonLayout;

std::vector<ActionId> actionsIn(Rect rect) {
  std::vector<ActionId> actions;
  for(const auto& placed : ribbonLayout(rect)) actions.push_back(placed.control.action);
  return actions;
}

bool shows(Rect rect, ActionId action) {
  const auto actions = actionsIn(rect);
  return std::find(actions.begin(), actions.end(), action) != actions.end();
}

// A column tall enough for exactly `n` controls, and not one more.
Rect columnFor(std::size_t n) {
  const float step = micronotes::ui::kRibbonButtonSize + micronotes::ui::kRibbonGap;
  return {0.0f, 0.0f, micronotes::ui::kRibbonWidth,
          2.0f * micronotes::ui::kRibbonEdgePad - micronotes::ui::kRibbonGap + step * static_cast<float>(n)};
}

}

MICRONOTES_TEST(ribbon_shows_every_control_when_there_is_room) {
  const auto actions = actionsIn({0.0f, 30.0f, 44.0f, 700.0f});
  MICRONOTES_REQUIRE(actions.size() == micronotes::ui::kRibbonControlCount);
  // Top group first, in the order the rail draws it.
  MICRONOTES_REQUIRE(actions.front() == ActionId::NewNote);
  MICRONOTES_REQUIRE(actions.back() == ActionId::Settings);
}

// Boxes are the same size, in one column, and never on top of each other. An
// overlap answers to whichever hit test ran last, so it is worse than a
// missing control.
MICRONOTES_TEST(ribbon_never_overlaps_two_controls) {
  for(std::size_t n = 0; n <= micronotes::ui::kRibbonControlCount + 2; ++n) {
    const Rect column = columnFor(n);
    const auto placement = ribbonLayout(column);
    float previousBottom = column.y;
    for(const auto& placed : placement) {
      MICRONOTES_REQUIRE(placed.rect.h == micronotes::ui::kRibbonButtonSize);
      MICRONOTES_REQUIRE(placed.rect.y >= previousBottom);
      MICRONOTES_REQUIRE(placed.rect.y + placed.rect.h <= column.y + column.h + 1.0f);
      previousBottom = placed.rect.y + placed.rect.h;
    }
  }
}

MICRONOTES_TEST(ribbon_fits_exactly_as_many_controls_as_the_column_holds) {
  for(std::size_t n = 0; n <= micronotes::ui::kRibbonControlCount; ++n) {
    MICRONOTES_REQUIRE(ribbonLayout(columnFor(n)).size() == n);
    // One pixel short of the room for the nth, and the nth is not drawn.
    Rect tight = columnFor(n);
    tight.h -= 1.0f;
    MICRONOTES_REQUIRE(ribbonLayout(tight).size() == (n > 0 ? n - 1 : 0));
  }
}

// The one guarantee the rail exists to make: with every panel put away, it is
// still the way back to them. A short window that dropped the sidebar toggle
// left a shell with no drawn route out of it, which is what this pins.
MICRONOTES_TEST(ribbon_gives_up_settings_before_the_panel_toggles) {
  // Room for six: Settings goes, both toggles stay.
  const Rect six = columnFor(6);
  MICRONOTES_REQUIRE(!shows(six, ActionId::Settings));
  MICRONOTES_REQUIRE(shows(six, ActionId::ToggleSidebar));
  MICRONOTES_REQUIRE(shows(six, ActionId::ToggleRightPanel));

  // Room for four: the right panel toggle and the palette have gone too, and
  // the sidebar toggle is still there.
  const Rect four = columnFor(4);
  MICRONOTES_REQUIRE(shows(four, ActionId::ToggleSidebar));
  MICRONOTES_REQUIRE(!shows(four, ActionId::ToggleRightPanel));
  MICRONOTES_REQUIRE(!shows(four, ActionId::CommandPalette));
  MICRONOTES_REQUIRE(shows(four, ActionId::NewNote));

  // Room for one, and it is the way back rather than anything else.
  const auto last = actionsIn(columnFor(1));
  MICRONOTES_REQUIRE(last.size() == 1);
  MICRONOTES_REQUIRE(last.front() == ActionId::ToggleSidebar);
}

// The foot group is anchored to the bottom edge as a group, so however many of
// it survived, the last one sits against the foot.
MICRONOTES_TEST(ribbon_keeps_the_foot_group_against_the_bottom) {
  for(std::size_t n = 1; n <= micronotes::ui::kRibbonControlCount; ++n) {
    const Rect column = columnFor(n);
    float lowestFoot = -1.0f;
    for(const auto& placed : ribbonLayout(column)) {
      if(placed.control.atFoot) lowestFoot = std::max(lowestFoot, placed.rect.y + placed.rect.h);
    }
    if(lowestFoot < 0.0f) continue;  // nothing from the foot group fitted
    MICRONOTES_REQUIRE(lowestFoot == column.y + column.h - micronotes::ui::kRibbonEdgePad);
  }
}

MICRONOTES_TEST(ribbon_draws_nothing_in_an_empty_rect) {
  MICRONOTES_REQUIRE(ribbonLayout({0.0f, 0.0f, 0.0f, 700.0f}).size() == 0);
  MICRONOTES_REQUIRE(ribbonLayout({0.0f, 0.0f, 44.0f, 0.0f}).size() == 0);
  MICRONOTES_REQUIRE(ribbonLayout({0.0f, 0.0f, 44.0f, -50.0f}).size() == 0);
}

// Every control names a real action, or the rail would draw a mark that runs
// nothing when clicked.
MICRONOTES_TEST(ribbon_controls_all_name_a_registered_action) {
  MICRONOTES_REQUIRE(micronotes::ui::ribbonControls().size() == micronotes::ui::kRibbonControlCount);
  for(const auto& control : micronotes::ui::ribbonControls()) {
    MICRONOTES_REQUIRE(control.action != ActionId::Count);
    MICRONOTES_REQUIRE(micronotes::ui::findAction(control.action) != nullptr);
  }
}
