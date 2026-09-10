#include "TestSupport.h"

#include "ui/ScrollList.h"

using micronotes::ui::RowStrip;
using micronotes::ui::ScrollList;
using micronotes::ui::WheelAccumulator;

// A precise pointing device delivers a stream of deltas well below one notch.
// Truncating each one to an int is how the sidebar and the note page used to
// scroll nothing at all under a trackpad while the editor and the reading pane,
// which carried a remainder, tracked the finger.
MICRONOTES_TEST(wheel_accumulates_deltas_below_one_notch) {
  WheelAccumulator wheel;
  int moved = 0;
  // Twenty events of a fiftieth of a notch is 0.4 of a notch: at 42 pixels a
  // notch that is 16 pixels of travel, and every single event on its own
  // truncates to zero.
  for(int i = 0; i < 20; ++i) moved += wheel.take(-0.02f, 42.0f);
  MICRONOTES_REQUIRE(moved > 0);
  MICRONOTES_REQUIRE(moved == 16);
}

MICRONOTES_TEST(wheel_keeps_a_discrete_notch_exact) {
  WheelAccumulator wheel;
  MICRONOTES_REQUIRE(wheel.take(-1.0f, 42.0f) == 42);
  MICRONOTES_REQUIRE(wheel.take(1.0f, 42.0f) == -42);
}

// Positive `wheel.y` is SDL's "content moves down", which is a smaller scroll
// offset, so the sign has to invert exactly once on the way through.
MICRONOTES_TEST(wheel_inverts_sdl_sign_once) {
  WheelAccumulator wheel;
  MICRONOTES_REQUIRE(wheel.take(3.0f, 3.0f) == -9);
}

// A gesture reversed mid-stroke must not bank travel it already gave back: the
// remainder is signed, so the two directions cancel rather than accumulate.
MICRONOTES_TEST(wheel_remainder_cancels_across_a_reversal) {
  WheelAccumulator wheel;
  int moved = 0;
  for(int i = 0; i < 10; ++i) moved += wheel.take(-0.05f, 42.0f);
  for(int i = 0; i < 10; ++i) moved += wheel.take(0.05f, 42.0f);
  MICRONOTES_REQUIRE(moved == 0);
}

// --- the list -----------------------------------------------------------
//
// The three operations the sidebar, the right panel, the raw pane and the page
// used to spell out for themselves. Written once here, they are testable once
// here -- which they were not: the ceiling was arithmetic inside four paint
// functions, and the one panel that had got it wrong shipped with no working
// scrollbar at all.

MICRONOTES_TEST(scroll_list_has_no_ceiling_when_the_content_fits) {
  ScrollList list;
  list.setContent(400.0f, 250.0f);
  MICRONOTES_REQUIRE(list.maxScroll() == 0);
  list.scrollTo(80);
  MICRONOTES_REQUIRE(list.scroll() == 0);
}

// Fractional overflow rounds *up*, or the last pixel of the last row is one the
// reader cannot reach.
MICRONOTES_TEST(scroll_list_rounds_its_ceiling_up) {
  ScrollList list;
  list.setContent(100.0f, 240.5f);
  MICRONOTES_REQUIRE(list.maxScroll() == 141);
}

// A viewport of zero -- a panel drawn before it has been given a rect -- must
// not make the ceiling the whole content plus one. The floor of one unit is
// what the four surfaces each wrote as `std::max(1.0f, ...)`.
MICRONOTES_TEST(scroll_list_floors_the_viewport_at_one) {
  ScrollList list;
  list.setContent(0.0f, 10.0f);
  MICRONOTES_REQUIRE(list.maxScroll() == 9);
}

// The bug the type exists to make impossible: the content shrinks under a list
// that is scrolled to the bottom, and the offset has to come with it. Written
// out per surface this was a second statement somebody had to remember.
MICRONOTES_TEST(scroll_list_pulls_the_offset_down_when_the_content_shrinks) {
  ScrollList list;
  list.setContent(100.0f, 500.0f);
  list.scrollTo(400);
  MICRONOTES_REQUIRE(list.scroll() == 400);
  list.setContent(100.0f, 200.0f);
  MICRONOTES_REQUIRE(list.scroll() == 100);
}

MICRONOTES_TEST(scroll_list_clamps_the_wheel_at_both_ends) {
  ScrollList list;
  list.setContent(100.0f, 300.0f);
  for(int i = 0; i < 20; ++i) list.wheel(-1.0f, 42.0f);
  MICRONOTES_REQUIRE(list.scroll() == 200);
  for(int i = 0; i < 20; ++i) list.wheel(1.0f, 42.0f);
  MICRONOTES_REQUIRE(list.scroll() == 0);
}

// `reveal` moves as little as it can, and not at all when the range is already
// showing -- a caret already on screen must not jump the page under it.
MICRONOTES_TEST(scroll_list_reveals_a_range_by_the_shortest_move) {
  ScrollList list;
  list.setContent(100.0f, 1000.0f);
  list.scrollTo(300);
  list.reveal(320.0f, 340.0f, 100.0f);
  MICRONOTES_REQUIRE(list.scroll() == 300);
  list.reveal(280.0f, 300.0f, 100.0f);
  MICRONOTES_REQUIRE(list.scroll() == 280);
  list.reveal(500.0f, 520.0f, 100.0f);
  MICRONOTES_REQUIRE(list.scroll() == 420);
}

// A partial gesture belongs to the content it was made over. Rebasing for a new
// note has to drop it, or the first event after the switch carries travel from
// the note before.
MICRONOTES_TEST(scroll_list_rebase_drops_the_wheel_remainder) {
  ScrollList list;
  list.setContent(100.0f, 1000.0f);
  list.wheel(-0.5f, 42.0f);
  list.rebase();
  MICRONOTES_REQUIRE(list.scroll() == 0);
  list.wheel(-0.5f, 42.0f);
  MICRONOTES_REQUIRE(list.scroll() == 21);
}

// --- RowStrip: a list scrolled by whole rows ------------------------------

// The ceiling is what the paint recorded, not a count the caller guessed. This
// is the coupling the type exists to hold: `shown` is written by the only code
// that can know it, and every clamp reads it back.
MICRONOTES_TEST(row_strip_clamps_against_what_the_paint_fitted) {
  RowStrip strip;
  strip.fitted(7);
  MICRONOTES_REQUIRE(strip.last(20) == 13);
  strip.scroll = 99;
  strip.clamp(20);
  MICRONOTES_REQUIRE(strip.scroll == 13);
}

// A list that fits entirely cannot be scrolled at all, and a list shorter than
// the pane must not report a negative ceiling -- which is what the longhand
// `count - shown` did at every one of the nine sites before a `std::max`.
MICRONOTES_TEST(row_strip_that_fits_does_not_scroll) {
  RowStrip strip;
  strip.fitted(20);
  MICRONOTES_REQUIRE(strip.last(5) == 0);
  strip.scrollBy(3, 5);
  MICRONOTES_REQUIRE(strip.scroll == 0);
}

// The paint always places its first row, however little room there is, so a
// pane that fitted nothing still reports one. A zero here would make `last()`
// the whole list and send the offset off the end.
MICRONOTES_TEST(row_strip_never_records_a_pane_of_no_rows) {
  RowStrip strip;
  strip.fitted(0);
  MICRONOTES_REQUIRE(strip.shown == 1);
  MICRONOTES_REQUIRE(strip.last(4) == 3);
}

// Following the keyboard: move as little as possible, at either edge, and never
// past the top.
MICRONOTES_TEST(row_strip_reveal_moves_the_minimum) {
  RowStrip strip;
  strip.fitted(5);
  strip.scroll = 10;
  strip.reveal(12);            // already on screen
  MICRONOTES_REQUIRE(strip.scroll == 10);
  strip.reveal(14);            // one past the last visible row
  MICRONOTES_REQUIRE(strip.scroll == 10);
  strip.reveal(15);
  MICRONOTES_REQUIRE(strip.scroll == 11);
  strip.reveal(3);
  MICRONOTES_REQUIRE(strip.scroll == 3);
  strip.reveal(0);
  MICRONOTES_REQUIRE(strip.scroll == 0);
}
