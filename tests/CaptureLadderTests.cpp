#include "TestSupport.h"

#include "app/CaptureLadder.h"

// `app/CaptureLadder.h`: which surfaces own the pointer from which.
//
// The rule was three booleans recombined at five places in `drawApp`, and
// getting one wrong has no symptom a test or a screenshot catches -- a row
// behind a palette lights up under the pointer and promises a click the
// palette then swallows. The combinations are the rule, so here they are,
// stated once each.

using micronotes::app::CaptureLadder;

MICRONOTES_TEST(nothing_captures_the_pointer_with_no_modal_up) {
  const CaptureLadder quiet(false, false, false);
  MICRONOTES_REQUIRE(!quiet.menuBar());
  MICRONOTES_REQUIRE(!quiet.panel());
  MICRONOTES_REQUIRE(!quiet.openMenu());
  MICRONOTES_REQUIRE(!quiet.settingsCard());
  MICRONOTES_REQUIRE(!quiet.overlays());
}

// The one asymmetry in the ladder, and the reason it is a ladder: sliding
// along the bar with a menu open is how a menu bar switches menus, so the bar
// stays live under its own popup while every panel below it does not.
MICRONOTES_TEST(an_open_menu_captures_the_panels_but_not_the_bar_it_hangs_from) {
  const CaptureLadder menu(false, false, true);
  MICRONOTES_REQUIRE(!menu.menuBar());
  MICRONOTES_REQUIRE(menu.panel());
  MICRONOTES_REQUIRE(!menu.openMenu());
  MICRONOTES_REQUIRE(!menu.settingsCard());
}

// The Settings card is drawn over the panels and over the menu bar, and under
// the overlay stack -- one of its rows opens the library prompt.
MICRONOTES_TEST(the_settings_card_captures_everything_below_it) {
  const CaptureLadder card(false, true, false);
  MICRONOTES_REQUIRE(card.menuBar());
  MICRONOTES_REQUIRE(card.panel());
  MICRONOTES_REQUIRE(card.openMenu());
  MICRONOTES_REQUIRE(!card.settingsCard());
  MICRONOTES_REQUIRE(!card.overlays());
}

// An overlay is the top of the stack, so everything under it is captured and
// it is captured by nothing.
MICRONOTES_TEST(an_overlay_captures_every_surface_under_it) {
  const CaptureLadder overlay(true, false, false);
  MICRONOTES_REQUIRE(overlay.menuBar());
  MICRONOTES_REQUIRE(overlay.panel());
  MICRONOTES_REQUIRE(overlay.openMenu());
  MICRONOTES_REQUIRE(overlay.settingsCard());
  MICRONOTES_REQUIRE(!overlay.overlays());
}

// The ladder only ever grows downward: a surface captured with fewer things up
// stays captured with more. Stated as a property over all eight states,
// because it is the invariant a hand-written combination breaks -- and it is
// what "ladder" means.
MICRONOTES_TEST(a_captured_surface_stays_captured_when_more_is_opened_over_it) {
  for(int state = 0; state < 8; ++state) {
    const bool overlay = (state & 1) != 0;
    const bool card = (state & 2) != 0;
    const bool menu = (state & 4) != 0;
    const CaptureLadder rung(overlay, card, menu);
    // Every rung is at least as captured as the one above it in paint order.
    micronotes::tests::require(!rung.settingsCard() || rung.openMenu(),
                               "the card is captured where the popup under it is not");
    micronotes::tests::require(!rung.openMenu() || rung.panel(),
                               "the popup is captured where the panels under it are not");
    micronotes::tests::require(!rung.overlays(), "the overlay stack is never captured");
    // And nothing is captured by nothing.
    if(!overlay && !card && !menu) {
      micronotes::tests::require(!rung.panel(), "a panel is captured with no modal up");
    }
  }
}
