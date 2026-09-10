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
//
// `RowStrip` below is the same idea for a list that scrolls by whole rows rather
// than by pixels, which is what a surface has to do when only the paint knows
// how tall its rows came out.

#include <cstddef>

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

// A list scrolled by whole rows, whose rows are not all the same height.
//
// `ScrollList`'s sibling, and the reason there are two: that one counts pixels
// against a content extent the layout knows in advance, and three surfaces here
// cannot work that way. The settings rows, the About entries and the overlay's
// list are all as tall as their own content -- a setting whose help wraps to two
// lines is taller than one whose help fits on one -- so the *only* thing that
// knows how many rows a pane held is the paint that placed them. `shown` is that
// number, written by the paint and read by everything that clamps.
//
// Naming the pair is the point. The settings card kept `rowScroll`/`rowsShown`
// for its settings and `aboutScroll`/`aboutRowsShown` for its About list, and
// the overlay kept its offset on the overlay and its count on the stack -- so
// the count belonged to whichever overlay was drawn last rather than to the list
// it was counted from. Around those, the clamp, the wheel, the arrow keys and
// the keyboard reveal were written out longhand at nine sites. A discrepancy
// between any two of them is a list that cannot be scrolled to its own end,
// which is a good deal worse than a wrong pixel.
struct RowStrip {
  // First row on screen, as a position in the list being shown.
  int scroll = 0;
  // Rows the last paint fitted. Never zero: a pane always shows its first row,
  // even clipped, so that a list too tall for its pane still reads as a list.
  int shown = 1;

  // The furthest this list may be scrolled: the offset that puts its last row
  // at the foot. It is also, and necessarily, the number of rows the pane could
  // not hold -- which is what the scrollbar's travel is measured in, so the two
  // cannot be given separate spellings and drift.
  int last(std::size_t count) const;

  void clamp(std::size_t count);
  // `rows` positive scrolls towards the end.
  void scrollBy(int rows, std::size_t count);
  // Move as little as possible to bring row `index` on screen.
  void reveal(int index);
  // Back to the top, for a list whose content has been replaced -- a filter
  // that has just matched something else entirely.
  void rebase() { scroll = 0; }

  // What the paint records when it is done placing rows.
  void fitted(std::size_t rows);
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

  // An offset to be judged by the next layout rather than by the last one.
  //
  // Every other way in clamps against the ceiling the previous frame recorded,
  // which is right for a wheel and wrong for restoring a remembered place: a
  // tab switch happens while the ceiling still belongs to the note being left,
  // so coming back to a long note from a short one clamped the offset to the
  // short note's end and lost the place. Written past the ceiling here and
  // clamped by `setContent` the moment the real content is known -- which is
  // also what keeps a place remembered while the note grew shorter elsewhere
  // from landing past its end.
  void restore(int value);

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
