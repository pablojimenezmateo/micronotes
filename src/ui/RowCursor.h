#pragma once

#include "ui/Rect.h"

#include <cstddef>
#include <limits>

namespace micronotes::ui {

// Placing a column of rows, one under the next, in one pass.
//
// This is the *producer* half of `rowBand`. That query is only correct for a
// list that tiles -- every row's box starting where the one before it ended --
// and until now the invariant was held by whichever function happened to be
// building the list agreeing to advance by exactly the height it had just used.
// Four of them did: the sidebar's builder, in seven lambdas sharing a running
// `y`; the settings card twice in one function, once for its settings and once
// for its About list, identical on eleven statements; and the overlay's list a
// fourth time. An advance nobody owns is an advance that gets written again, and
// the copies then keep separate fields for the same two concepts -- which is
// exactly what `rowScroll`/`aboutScroll` and `rowsShown`/`aboutRowsShown` were.
//
// The cursor owns the advance instead, at the end that establishes it, and
// `rowBand` reads the result at the end that depends on it.
class RowCursor {
public:
  // A column `width` wide at `x`, whose first row's top edge is `top`.
  //
  // For a surface that scrolls by pixels, `top` is the list's origin with the
  // scroll already taken off, so the boxes come out in window coordinates and
  // `height()` still reports the content. For one that scrolls by whole rows,
  // `top` is the pane's own top and the scroll picked which row comes first.
  RowCursor(float x, float width, float top) : x_(x), width_(width), top_(top), y_(top) {}

  // The box for a row `height` tall, with the cursor left directly under it.
  // `inset` narrows the box at both edges without moving the column, which is
  // how a list gives its rows air inside the panel.
  Rect place(float height, float inset = 0.0f) {
    const Rect row {x_ + inset, y_, width_ - inset * 2.0f, height};
    y_ += height;
    ++placed_;
    return row;
  }

  // A foot the rows may not be cut by, for a pane that stops rather than
  // scrolling past it. Unset means the list runs as long as it likes, which is
  // what a surface that scrolls in pixels and culls with `rowBand` wants.
  void stopAt(float bottom) { bottom_ = bottom; }

  // Whether a row `height` tall still clears the foot.
  //
  // The first row always does, however little room there is. A pane that drew
  // nothing because its one row was a pixel too tall is a pane with no way to
  // say what happened; a clipped first row at least reads as a list that
  // continues. Asked *before* the row is placed rather than after, because
  // breaking on "the last row started past the foot" leaves a row cut through
  // the middle of its glyphs, which reads as a rendering fault.
  bool fits(float height) const { return placed_ == 0 || y_ + height <= bottom_; }

  // Where the next row would start, and how many have been placed.
  float y() const { return y_; }
  std::size_t placed() const { return placed_; }

  // Everything placed so far. For a list built in one pass this is its content
  // height, which is what a scroll ceiling is measured against.
  float height() const { return y_ - top_; }

  // The mean row, in pixels: the scale that turns an offset counted in rows
  // into the pixel one `drawVerticalScrollbar` works in. A list whose rows are
  // not all the same height has no other honest conversion, and at this pitch
  // both the visible fraction and the thumb's travel come out the same as if
  // the whole list had been measured. Zero when nothing was placed.
  float pitch() const {
    return placed_ == 0 ? 0.0f : height() / static_cast<float>(placed_);
  }

private:
  float x_;
  float width_;
  float top_;
  float y_;
  float bottom_ = std::numeric_limits<float>::max();
  std::size_t placed_ = 0;
};

}
