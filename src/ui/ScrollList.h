#pragma once

// A surface that scrolls: the offset, how far it may go, and the wheel.
//
// Four surfaces in this shell scroll -- the sidebar, the right-hand panel, the
// raw pane and the page -- and each of them used to spell the same three
// operations out for itself:
//
//   * the ceiling, `max(0, ceil(content - viewport))`, with the viewport inset
//     differing per surface and nothing saying why;
//   * the clamp, `scroll = clamp(scroll, 0, maxScroll)`, at six sites;
//   * the wheel, `scroll = clamp(scroll + wheel.take(...), 0, maxScroll)`, at
//     five.
//
// Written out per surface, a surface is only scrollable once somebody remembers
// all three for it -- and one already had not been: the sidebar shipped with no
// working scrollbar because the ceiling was never recorded. Here the three are
// one type, so a new panel scrolls by holding one.
namespace micronotes::ui {

// A wheel gesture accumulated to whole units.
//
// SDL reports `wheel.y` in notches for a discrete wheel and in fractions of a
// notch for a precise one: a trackpad delivers a stream of deltas well below
// 1.0. Truncating each event to an int discards them, so a slow gesture scrolls
// nothing at all and a fast one moves in visible jumps. Carrying the remainder
// across events makes the movement track the finger.
struct WheelAccumulator {
  float remainder = 0.0f;

  // Whole units to scroll by, positive downwards. `notches` is SDL's sign
  // convention, where a positive value means the content moves down.
  int take(float notches, float unitsPerNotch);
};

class ScrollList {
public:
  int scroll() const { return scroll_; }
  int maxScroll() const { return maxScroll_; }

  // The ceiling, from what the surface last laid out: `content` units shown
  // through a window `viewport` units tall, in whichever unit the surface
  // scrolls by -- pixels for three of the four, whole rows for the raw pane.
  //
  // Recording it here rather than recomputing it on demand is what lets a wheel
  // event clamp against what was actually drawn. The raw pane did recompute,
  // and the recomputation was a soft wrap of the whole note: four call sites,
  // one of them the wheel, each paying 807 us on a 200 KB note to answer a
  // question the previous frame had already answered.
  //
  // Clamps the offset with it, because a ceiling that moves without the offset
  // following is a list scrolled past its own bottom.
  void setContent(float viewport, float content);

  void scrollTo(int value);
  void scrollBy(int delta);

  // One wheel event, in SDL's sign convention.
  void wheel(float notches, float unitsPerNotch);

  // Move as little as possible to bring `[top, bottom)` inside a window
  // `height` units tall that starts at the current offset. Both are in the same
  // space the offset counts in.
  void reveal(float top, float bottom, float height);

  // Back to the top, with the wheel's remainder dropped -- a partial gesture
  // belongs to the content it was made over, not to whatever replaced it.
  void rebase();

private:
  int scroll_ = 0;
  int maxScroll_ = 0;
  WheelAccumulator wheel_;
};

}
