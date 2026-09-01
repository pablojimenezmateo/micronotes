#include "TestSupport.h"

#include "app/FramePolicy.h"

using micronotes::app::chooseWait;
using micronotes::app::FrameDeadlines;
using micronotes::app::IdleHint;
using micronotes::app::shouldYieldEventDrain;
using micronotes::app::soonestDeadline;
using micronotes::app::WaitMode;

MICRONOTES_TEST(frame_policy_blocks_when_there_is_nothing_to_wake_for) {
  const auto decision = chooseWait({IdleHint::Idle, -1, -1, -1, -1});
  MICRONOTES_REQUIRE(decision.mode == WaitMode::Block);
  MICRONOTES_REQUIRE(decision.timeoutMs == 0);
}

MICRONOTES_TEST(frame_policy_does_not_sleep_while_something_is_moving) {
  const auto decision = chooseWait({IdleHint::Busy, 500, 500, 16, 4000});
  MICRONOTES_REQUIRE(decision.mode == WaitMode::Poll);
  MICRONOTES_REQUIRE(decision.timeoutMs == 0);
}

// The wake has to be the soonest of everything that wants one, or whichever
// deadline lost is served late.
MICRONOTES_TEST(frame_policy_wakes_for_the_soonest_deadline) {
  MICRONOTES_REQUIRE(chooseWait({IdleHint::Blinking, 900, 530, -1, 4000}).timeoutMs == 530);
  MICRONOTES_REQUIRE(chooseWait({IdleHint::Blinking, 40, 530, -1, 4000}).timeoutMs == 40);
  MICRONOTES_REQUIRE(chooseWait({IdleHint::Idle, -1, -1, -1, 20}).timeoutMs == 20);
  MICRONOTES_REQUIRE(soonestDeadline({IdleHint::Idle, -1, -1, -1, -1}) == -1);
}

// A zero timeout is a busy loop wearing a timeout's clothes.
MICRONOTES_TEST(frame_policy_never_asks_for_a_zero_timeout) {
  const auto decision = chooseWait({IdleHint::Blinking, 0, -1, -1, -1});
  MICRONOTES_REQUIRE(decision.mode == WaitMode::Timeout);
  MICRONOTES_REQUIRE(decision.timeoutMs == 1);
  // Even a deadline in the past, which a slow frame can produce.
  MICRONOTES_REQUIRE(chooseWait({IdleHint::Blinking, -1, 0, -1, -1}).timeoutMs == 1);
}

// The drain yields only when there is something to show for stopping.
MICRONOTES_TEST(frame_policy_yields_the_drain_only_with_a_frame_owed) {
  MICRONOTES_REQUIRE(!shouldYieldEventDrain(1000, false));
  MICRONOTES_REQUIRE(shouldYieldEventDrain(512, true));
  MICRONOTES_REQUIRE(!shouldYieldEventDrain(511, true));
  MICRONOTES_REQUIRE(shouldYieldEventDrain(3, true, 3));
}
