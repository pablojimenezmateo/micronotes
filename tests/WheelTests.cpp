#include "TestSupport.h"

#include "app/Shell.h"

using micronotes::app::WheelAccumulator;

// A precise pointing device delivers a stream of deltas well below one notch.
// Truncating each one to an int is how the sidebar and the live page used to
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
