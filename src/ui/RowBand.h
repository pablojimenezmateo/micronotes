#pragma once

#include "ui/Rect.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace micronotes::ui {

// The half-open range of indices in a list of rows whose boxes meet the band
// [top, bottom], for a list that **tiles**: laid out in one pass top to bottom,
// each row's box starting where the one before it ended. Both `rect.y` and
// `rect.y + rect.h` are then non-decreasing across the list, which is what makes
// the two edges of the band partition points rather than a scan.
//
// This is `DocumentLayout::blockRange` for a shell list, and it exists for the
// same reason: a draw that walks every row and tests each one against its
// viewport costs the *list* per frame however little of it is on screen, and a
// hit test written the same way costs the list per mouse-motion event -- a
// hundred a second while a cursor crosses the panel. Both bounds grow with the
// library while the answer stays a dozen rows.
//
// Both edges are inclusive, which is what `contains` means everywhere else in
// the shell: a row whose bottom just touches `top`, or whose top just touches
// `bottom`, is in the band. A zero-height band is therefore "the rows this y is
// inside", which is the hit test.
//
// `probes` is added to rather than assigned, so a caller can charge the search
// steps to its own counter and see the bound hold.
template <typename RectAt>
std::pair<std::size_t, std::size_t> rowBand(std::size_t count, float top, float bottom,
                                            const RectAt& rectAt, std::uint64_t* probes = nullptr) {
  std::uint64_t steps = 0;
  const auto search = [&](auto before) {
    std::size_t lo = 0;
    std::size_t hi = count;
    while(lo < hi) {
      ++steps;
      const std::size_t mid = lo + (hi - lo) / 2;
      if(before(rectAt(mid))) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  };
  // The first row whose bottom has reached the band, then the first row whose
  // top has passed it.
  const std::size_t begin = search([top](const Rect& rect) { return rect.y + rect.h < top; });
  const std::size_t end = search([bottom](const Rect& rect) { return rect.y <= bottom; });
  if(probes) *probes += steps;
  return {begin, begin > end ? begin : end};
}

}
