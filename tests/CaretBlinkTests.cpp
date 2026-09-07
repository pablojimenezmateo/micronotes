#include "TestSupport.h"

#include "ui/CaretBlink.h"

using micronotes::ui::CaretBlink;
using micronotes::ui::kCaretBlinkIntervalMs;
using micronotes::ui::kCaretBlinkSettleMs;

// A caret nobody has told anything to is solid, not invisible.
//
// The direction of that default matters: a caret that starts hidden and waits
// for a tick is a field that looks unfocused on the frame it gains focus.
MICRONOTES_TEST(caret_blink_starts_solid) {
  CaretBlink blink;
  MICRONOTES_REQUIRE(blink.visible(0));
  MICRONOTES_REQUIRE(blink.visible(999999));
  // And asks for no wake-ups, so a window with no caret in it sleeps.
  MICRONOTES_REQUIRE(blink.waitMs(0) < 0);
}

// The blink itself: on, off, on, at the interval.
MICRONOTES_TEST(caret_blink_alternates_at_the_interval) {
  CaretBlink blink;
  blink.observe(1, 1000);
  // The phase it starts in is the *on* phase, because the thing that moved the
  // caret was a keystroke and the reader is looking for where it landed.
  MICRONOTES_REQUIRE(blink.visible(1000));
  MICRONOTES_REQUIRE(blink.visible(1000 + kCaretBlinkIntervalMs - 1));
  MICRONOTES_REQUIRE(!blink.visible(1000 + kCaretBlinkIntervalMs));
  MICRONOTES_REQUIRE(!blink.visible(1000 + kCaretBlinkIntervalMs * 2 - 1));
  MICRONOTES_REQUIRE(blink.visible(1000 + kCaretBlinkIntervalMs * 2));

  // The wait is always the distance to the next phase change, so the frame
  // that flips the caret is the frame the loop wakes for.
  MICRONOTES_REQUIRE(blink.waitMs(1000) == static_cast<int>(kCaretBlinkIntervalMs));
  MICRONOTES_REQUIRE(blink.waitMs(1000 + kCaretBlinkIntervalMs - 10) == 10);
}

// Typing shows a solid caret. Every keystroke restarts the blink, which is what
// stops a caret from strobing under a moving cursor.
MICRONOTES_TEST(caret_blink_restarts_when_the_caret_moves) {
  CaretBlink blink;
  blink.observe(1, 1000);
  // Part-way into the off phase...
  const Uint64 off = 1000 + kCaretBlinkIntervalMs + 100;
  MICRONOTES_REQUIRE(!blink.visible(off));
  // ...a keystroke, and the caret is solid again from that instant.
  blink.observe(2, off);
  MICRONOTES_REQUIRE(blink.visible(off));
  MICRONOTES_REQUIRE(blink.visible(off + kCaretBlinkIntervalMs - 1));
  MICRONOTES_REQUIRE(!blink.visible(off + kCaretBlinkIntervalMs));

  // The same key twice is not a move: observing it again must not restart the
  // phase, or a caret would never blink at all -- the frame loop calls this
  // every frame with the same key while nothing is happening.
  blink.observe(2, off + kCaretBlinkIntervalMs);
  MICRONOTES_REQUIRE(!blink.visible(off + kCaretBlinkIntervalMs));
}

// And it stops. This is the half that keeps an idle window asleep: a caret
// blinking forever is sixty wake-ups a minute for as long as the app is open.
MICRONOTES_TEST(caret_blink_settles_solid_and_stops_asking_to_be_woken) {
  CaretBlink blink;
  blink.observe(1, 0);
  MICRONOTES_REQUIRE(blink.waitMs(0) > 0);
  // Solid once settled -- solid rather than hidden, because a hidden caret
  // with nothing scheduled to bring it back is a caret that has vanished.
  MICRONOTES_REQUIRE(blink.visible(kCaretBlinkSettleMs));
  MICRONOTES_REQUIRE(blink.visible(kCaretBlinkSettleMs * 4));
  MICRONOTES_REQUIRE(blink.waitMs(kCaretBlinkSettleMs) < 0);
  MICRONOTES_REQUIRE(blink.waitMs(kCaretBlinkSettleMs * 4) < 0);

  // The frame the blink settles on is one the loop still has to wake for, or a
  // caret caught mid-off-phase would stay off until something else happened.
  const int wait = blink.waitMs(kCaretBlinkSettleMs - 5);
  MICRONOTES_REQUIRE(wait > 0 && wait <= 5);

  // And typing after the settle starts it blinking again.
  blink.observe(2, kCaretBlinkSettleMs * 4);
  MICRONOTES_REQUIRE(blink.waitMs(kCaretBlinkSettleMs * 4) > 0);
  MICRONOTES_REQUIRE(!blink.visible(kCaretBlinkSettleMs * 4 + kCaretBlinkIntervalMs));
}

// A clock that appears to go backwards reads as "just moved" rather than as an
// enormous elapsed time. `SDL_GetTicks` will not do this, but nothing about
// this class should depend on that.
MICRONOTES_TEST(caret_blink_survives_a_clock_that_goes_backwards) {
  CaretBlink blink;
  blink.observe(1, 5000);
  MICRONOTES_REQUIRE(blink.visible(4000));
  MICRONOTES_REQUIRE(blink.waitMs(4000) < 0);
}
