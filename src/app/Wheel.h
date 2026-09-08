#pragma once

#include <cmath>

// How far one notch of the wheel moves each scrolling surface, and the
// arithmetic that makes a trackpad work.
namespace micronotes::app {

constexpr int kEditorPageLines = 20;

// How far one notch of the wheel moves each surface. In lines for the raw
// editor, which scrolls by row; in pixels for everything else, which scrolls by
// distance. Together here rather than beside their own call sites, because the
// only way to tell whether two surfaces scroll at the same rate is to read the
// numbers next to each other.
constexpr float kEditorScrollLinesPerNotch = 3.0f;
constexpr float kViewerScrollPixelsPerNotch = 42.0f;
constexpr float kLiveScrollPixelsPerNotch = 42.0f;
constexpr float kSidebarScrollPixelsPerNotch = 42.0f;
constexpr float kRightPanelScrollPixelsPerNotch = 42.0f;

// A wheel gesture accumulated to whole units.
//
// SDL reports `wheel.y` in notches for a discrete wheel and in fractions of a
// notch for a precise one: a trackpad delivers a stream of deltas well below
// 1.0. Truncating each event to an int discards them, so a slow gesture scrolls
// nothing at all and a fast one moves in visible jumps. Carrying the remainder
// across events makes the movement track the finger.
//
// Every scrolling surface owns one. It used to be two floats on the runtime
// with the arithmetic written out at each site, which is why the sidebar and the
// live page -- the two surfaces added after it -- did not get it.
struct WheelAccumulator {
  float remainder = 0.0f;

  // Whole units to scroll by, positive downwards. `notches` is SDL's sign
  // convention, where a positive value means the content moves down.
  int take(float notches, float unitsPerNotch) {
    remainder += -notches * unitsPerNotch;
    const float whole = std::trunc(remainder);
    remainder -= whole;
    return static_cast<int>(whole);
  }
};

}
