#pragma once

#include <algorithm>
#include <cstdint>

// How long to sleep, and how much of the event queue to take in one go.
//
// Pure functions over plain data, so the loop's timing decisions can be checked
// in a test rather than by watching a CPU meter.
namespace micronotes::app {

// How busy the shell expects to be.
enum class IdleHint {
  // Something is animating, or a deadline has already passed. Do not sleep.
  Busy,
  // Nothing is moving except the caret, which has to blink on its own schedule.
  Blinking,
  // Nothing at all. Sleep until something happens.
  Idle
};

enum class WaitMode {
  Poll,
  Timeout,
  Block
};

struct WaitDecision {
  WaitMode mode = WaitMode::Block;
  int timeoutMs = 0;

  friend bool operator==(const WaitDecision&, const WaitDecision&) = default;
};

// Everything the loop knows about when it next has to be awake. A deadline of
// -1 means "there isn't one".
struct FrameDeadlines {
  IdleHint hint = IdleHint::Idle;
  int autosaveMs = -1;
  int caretBlinkMs = -1;
  int animationMs = -1;
  int notificationMs = -1;
};

// The soonest of the deadlines that exist, or -1 when none do.
inline int soonestDeadline(const FrameDeadlines& deadlines) {
  int soonest = -1;
  for(const int deadline : {deadlines.autosaveMs, deadlines.caretBlinkMs, deadlines.animationMs,
                            deadlines.notificationMs}) {
    if(deadline < 0) continue;
    soonest = soonest < 0 ? deadline : std::min(soonest, deadline);
  }
  return soonest;
}

// The loop used to block until the next autosave whatever else was pending, and
// to repaint on every event that arrived. This is the other half of that: when
// there is nothing to be awake for, sleep until the window says otherwise.
inline WaitDecision chooseWait(const FrameDeadlines& deadlines) {
  if(deadlines.hint == IdleHint::Busy) return {WaitMode::Poll, 0};
  const int soonest = soonestDeadline(deadlines);
  if(soonest < 0) return {WaitMode::Block, 0};
  // Never zero: a zero timeout is a busy loop wearing a timeout's clothes, and
  // a deadline that has already passed is served by the very next frame anyway.
  return {WaitMode::Timeout, std::max(1, soonest)};
}

// How many events one wake may drain before the frame is drawn.
//
// Draining the whole queue sounds thorough and is how a held key or a fast
// trackpad starves the paint: the queue refills as fast as it empties and the
// window stops updating while input is still arriving.
inline constexpr int kMaxEventsPerDrain = 512;

inline bool shouldYieldEventDrain(int processed, bool redrawPending, int budget = kMaxEventsPerDrain) {
  return redrawPending && processed >= budget;
}

}
