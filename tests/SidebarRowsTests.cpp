#include "TestSupport.h"

#include "ui/RowBand.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using micronotes::ui::Rect;
using micronotes::ui::rowBand;

namespace {

// A row list shaped like the sidebar's: rows tile the panel top to bottom,
// every row starting where the one before it ended, and heights varying because
// a search result is as tall as the lines under it.
std::vector<Rect> tiledRows(std::size_t count, float top) {
  std::vector<Rect> rows;
  rows.reserve(count);
  float y = top;
  for(std::size_t i = 0; i < count; ++i) {
    const float height = 20.0f + static_cast<float>(i % 3) * 7.0f;
    rows.push_back({8.0f, y, 200.0f, height});
    y += height;
  }
  return rows;
}

std::pair<std::size_t, std::size_t> band(const std::vector<Rect>& rows, float top, float bottom,
                                         std::uint64_t* probes = nullptr) {
  return rowBand(rows.size(), top, bottom, [&rows](std::size_t i) { return rows[i]; }, probes);
}

// The test each of the two walks this replaced applied, verbatim.
std::pair<std::size_t, std::size_t> bandByWalking(const std::vector<Rect>& rows, float top, float bottom) {
  std::size_t first = rows.size();
  std::size_t last = 0;
  for(std::size_t i = 0; i < rows.size(); ++i) {
    if(rows[i].y + rows[i].h < top || rows[i].y > bottom) continue;
    if(first == rows.size()) first = i;
    last = i + 1;
  }
  return first == rows.size() ? std::pair<std::size_t, std::size_t> {0, 0}
                              : std::pair<std::size_t, std::size_t> {first, last};
}

}

// The band has to be exactly the rows the walk would have drawn. One row wider
// at either edge is a row drawn twice or a search snippet trimmed for nothing;
// one row narrower is a row missing from the panel.
MICRONOTES_TEST(row_band_matches_the_walk_it_replaced) {
  const auto rows = tiledRows(400, 30.0f);
  const float end = rows.back().y + rows.back().h;
  for(float y = 0.0f; y < end + 40.0f; y += 3.0f) {
    for(const float height : {0.0f, 1.0f, 240.0f, 4000.0f}) {
      const auto got = band(rows, y, y + height);
      const auto want = bandByWalking(rows, y, y + height);
      // An empty band is an empty band wherever it sits, so the two agree when
      // neither draws anything; otherwise both ends have to match exactly.
      const bool same = (got.first == got.second && want.first == want.second) || got == want;
      micronotes::tests::require(same,
                                 "band [" + std::to_string(y) + ", " + std::to_string(y + height) +
                                     "]: got [" + std::to_string(got.first) + ", " +
                                     std::to_string(got.second) + ") want [" +
                                     std::to_string(want.first) + ", " +
                                     std::to_string(want.second) + ")");
    }
  }
}

// A band above or below every row selects nothing rather than the nearest row,
// and an empty list answers the same way rather than reading rect zero.
MICRONOTES_TEST(row_band_is_empty_off_either_end_of_the_list) {
  const auto rows = tiledRows(20, 100.0f);
  const auto above = band(rows, 0.0f, 50.0f);
  MICRONOTES_REQUIRE(above.first == above.second);
  const auto below = band(rows, 10000.0f, 10100.0f);
  MICRONOTES_REQUIRE(below.first == below.second);

  const std::vector<Rect> none;
  const auto nothing = band(none, 0.0f, 100.0f);
  MICRONOTES_REQUIRE(nothing.first == 0);
  MICRONOTES_REQUIRE(nothing.second == 0);
}

// A zero-height band is the hit test: the rows a single y is inside. Rows tile,
// so a y exactly on a boundary is inside two of them and both are in the band --
// the caller takes the first, which is the answer the walk gave.
MICRONOTES_TEST(row_band_of_no_height_is_the_row_under_a_point) {
  const auto rows = tiledRows(30, 0.0f);
  for(std::size_t i = 0; i < rows.size(); ++i) {
    const float middle = rows[i].y + rows[i].h / 2.0f;
    const auto inside = band(rows, middle, middle);
    MICRONOTES_REQUIRE(inside.first == i);
    MICRONOTES_REQUIRE(inside.second == i + 1);
  }
  const float seam = rows[4].y + rows[4].h;
  const auto onSeam = band(rows, seam, seam);
  MICRONOTES_REQUIRE(onSeam.first == 4);
  MICRONOTES_REQUIRE(onSeam.second == 6);
}

// The bound that matters: O(log rows), not O(library). The draw asks once a
// frame and the hit test once per mouse-motion event, so a walk was paid a
// hundred times a second while the cursor crossed the panel.
MICRONOTES_TEST(row_band_binary_searches_the_list) {
  const auto rows = tiledRows(4000, 30.0f);
  std::uint64_t probes = 0;
  const auto range = band(rows, 4000.0f, 4240.0f, &probes);
  MICRONOTES_REQUIRE(range.second > range.first);
  MICRONOTES_REQUIRE(range.second - range.first < 20);
  // Two searches over 4,000 rows is 2 * 12 steps. A walk would take 4,000.
  MICRONOTES_REQUIRE(probes < 28);
}
