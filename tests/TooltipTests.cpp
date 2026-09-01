#include "TestSupport.h"

#include "ui/Metrics.h"
#include "ui/Tooltip.h"

using micronotes::ui::HoverTooltip;
using micronotes::ui::placeTooltip;
using micronotes::ui::Rect;

namespace {

const Rect kWindow {0.0f, 0.0f, 1000.0f, 600.0f};

bool within(const Rect& outer, const Rect& inner) {
  return inner.x >= outer.x - 0.001f && inner.y >= outer.y - 0.001f &&
         inner.x + inner.w <= outer.x + outer.w + 0.001f &&
         inner.y + inner.h <= outer.y + outer.h + 0.001f;
}

}

MICRONOTES_TEST(tooltip_is_centred_under_the_control) {
  const Rect control {400.0f, 100.0f, 60.0f, 24.0f};
  const Rect card = placeTooltip(control, 120.0f, 28.0f, kWindow);
  // Centred on the control, not on the pointer, so it stays put while the
  // pointer moves inside the control.
  MICRONOTES_REQUIRE(card.x + card.w / 2.0f == control.x + control.w / 2.0f);
  MICRONOTES_REQUIRE(card.y == control.y + control.h + micronotes::ui::kTooltipGap);
}

MICRONOTES_TEST(tooltip_flips_above_when_there_is_no_room_below) {
  const Rect control {400.0f, 560.0f, 60.0f, 24.0f};
  const Rect card = placeTooltip(control, 120.0f, 28.0f, kWindow);
  MICRONOTES_REQUIRE(card.y + card.h <= control.y);
  MICRONOTES_REQUIRE(within(kWindow, card));
}

// A control at the very edge still gets a whole tooltip, moved sideways rather
// than half off the window.
MICRONOTES_TEST(tooltip_is_clamped_inside_the_window) {
  MICRONOTES_REQUIRE(within(kWindow, placeTooltip({0.0f, 100.0f, 20.0f, 20.0f}, 200.0f, 28.0f, kWindow)));
  MICRONOTES_REQUIRE(within(kWindow, placeTooltip({980.0f, 100.0f, 20.0f, 20.0f}, 200.0f, 28.0f, kWindow)));
  MICRONOTES_REQUIRE(within(kWindow, placeTooltip({500.0f, 0.0f, 20.0f, 20.0f}, 100.0f, 28.0f, kWindow)));
}

// A tooltip wider than the window cannot fit; it is pinned to the left edge
// rather than being placed at a negative x that would clip its first words.
MICRONOTES_TEST(tooltip_wider_than_the_window_starts_at_the_edge) {
  const Rect card = placeTooltip({500.0f, 100.0f, 20.0f, 20.0f}, 2000.0f, 28.0f, kWindow);
  MICRONOTES_REQUIRE(card.x == kWindow.x);
}

MICRONOTES_TEST(tooltip_with_no_text_is_not_showing) {
  MICRONOTES_REQUIRE(!HoverTooltip {}.showing());
  const HoverTooltip named {"something", {}};
  MICRONOTES_REQUIRE(named.showing());
}
