#include "TestSupport.h"

#include "ui/RowBand.h"
#include "ui/RowCursor.h"

#include <vector>

using micronotes::ui::Rect;
using micronotes::ui::RowCursor;
using micronotes::ui::rowBand;

// The invariant the whole pairing rests on: `rowBand` binary-searches a list,
// which is only correct while the rows tile. A cursor that placed a gap or an
// overlap would make the search answer for rows that are not there, silently.
MICRONOTES_TEST(row_cursor_tiles_what_it_places) {
  RowCursor cursor(10.0f, 200.0f, 50.0f);
  const std::vector<float> heights {20.0f, 8.0f, 34.0f, 20.0f};
  std::vector<Rect> rows;
  for(const float height : heights) rows.push_back(cursor.place(height));

  for(std::size_t i = 1; i < rows.size(); ++i) {
    MICRONOTES_REQUIRE(rows[i].y == rows[i - 1].y + rows[i - 1].h);
  }
  MICRONOTES_REQUIRE(rows.front().y == 50.0f);
  MICRONOTES_REQUIRE(cursor.height() == 82.0f);
  MICRONOTES_REQUIRE(cursor.placed() == 4);

  // And the query agrees with the producer, which is the point of them being
  // two halves of one thing. On the seam between two rows both are in the band,
  // because `rowBand`'s edges are inclusive -- so the hit test at a boundary
  // pixel answers with the row above *and* the row below rather than neither.
  const auto seam = rowBand(rows.size(), 78.0f, 78.0f, [&](std::size_t i) { return rows[i]; });
  MICRONOTES_REQUIRE(seam.first == 1);
  MICRONOTES_REQUIRE(seam.second == 3);
  const auto inside = rowBand(rows.size(), 90.0f, 90.0f, [&](std::size_t i) { return rows[i]; });
  MICRONOTES_REQUIRE(inside.first == 2);
  MICRONOTES_REQUIRE(inside.second == 3);
}

// An inset narrows the box without moving the column, so a list can give its
// rows air at the panel edges and still tile.
MICRONOTES_TEST(row_cursor_inset_narrows_without_moving_the_column) {
  RowCursor cursor(10.0f, 200.0f, 0.0f);
  const Rect full = cursor.place(20.0f);
  const Rect inset = cursor.place(20.0f, 6.0f);
  MICRONOTES_REQUIRE(full.x == 10.0f && full.w == 200.0f);
  MICRONOTES_REQUIRE(inset.x == 16.0f && inset.w == 188.0f);
  MICRONOTES_REQUIRE(inset.y == full.y + full.h);
}

// The foot refuses a row it would cut -- but never the first one. A pane that
// drew nothing because its one row was a pixel too tall has no way to say what
// happened; a clipped first row at least reads as a list that continues.
MICRONOTES_TEST(row_cursor_foot_always_admits_the_first_row) {
  RowCursor cursor(0.0f, 100.0f, 0.0f);
  cursor.stopAt(10.0f);
  MICRONOTES_REQUIRE(cursor.fits(40.0f));
  cursor.place(40.0f);
  MICRONOTES_REQUIRE(!cursor.fits(1.0f));
  MICRONOTES_REQUIRE(cursor.placed() == 1);
}

// A row that ends exactly on the foot is in: the band's edges are inclusive
// everywhere else in the shell, and a list that dropped its last row because it
// landed flush would leave a gap under it.
MICRONOTES_TEST(row_cursor_foot_admits_a_row_that_ends_on_it) {
  RowCursor cursor(0.0f, 100.0f, 0.0f);
  cursor.stopAt(30.0f);
  cursor.place(20.0f);
  MICRONOTES_REQUIRE(cursor.fits(10.0f));
  MICRONOTES_REQUIRE(!cursor.fits(11.0f));
}

// The scrollbar's scale for a list scrolled by whole rows whose rows are not
// all the same height: the mean row, so the thumb's travel matches what a
// measured list would have given.
MICRONOTES_TEST(row_cursor_pitch_is_the_mean_row) {
  RowCursor cursor(0.0f, 100.0f, 0.0f);
  MICRONOTES_REQUIRE(cursor.pitch() == 0.0f);
  cursor.place(10.0f);
  cursor.place(30.0f);
  MICRONOTES_REQUIRE(cursor.pitch() == 20.0f);
}
