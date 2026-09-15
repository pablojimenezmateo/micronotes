#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace microcore::util {

// Replacing a band in the middle of a sorted vector, with the tail moved once.
//
// Four readouts in this tree are bounded the same way -- keep the head, redo
// the middle, shift the tail: `doc::DocumentLayout`'s four parallel arrays,
// `editor::softWrapUpdate`'s rows, `util::findAllUpdate`'s matches and
// `ui::outlineUpdate`'s entries. *Finding* the band is different in each of
// them and belongs to each of them: a row band is a logical line, a match band
// is found by walking until the scan lands on a boundary the old list had, a
// block band is what the relay names. What was the same four times, written out
// four times, is what happens once the band is known -- and two of the four
// spelled it `erase` then `insert`, which moves the tail twice.
//
// So this is the part that is one algorithm, and the part that is not stays
// with its caller. Nothing here knows what an element is or what "sorted"
// means for it; the shift that follows a splice is the caller's too, because
// only the caller knows which of an element's fields are offsets.
//
// (The fifth bounded readout, the status bar's caret walk, has no list at all
// -- it counts newlines from an anchor -- so it is not an instance of this and
// pulling it in would have been a shape rather than a subject.)

// Makes the band of `oldCount` elements at `first` into a band of `newCount`,
// resizing `items` and moving the elements after the band exactly once. The
// band's own elements are left unspecified and the caller must write all
// `newCount` of them.
//
// Growing resizes first and moves backwards; shrinking moves first and resizes
// after. Either way the tail is read once and written once, which is what
// `erase` followed by `insert` is not: that moves the tail down to close the
// hole and then back up to open it again.
template <class Vector>
void openBand(Vector& items, std::size_t first, std::size_t oldCount, std::size_t newCount) {
  using Diff = typename std::iterator_traits<decltype(items.begin())>::difference_type;
  const auto at = [&items](std::size_t index) {
    return items.begin() + static_cast<Diff>(index);
  };
  const std::size_t was = items.size();
  const std::size_t bandEnd = first + oldCount;
  if(newCount > oldCount) {
    items.resize(was + (newCount - oldCount));
    std::move_backward(at(bandEnd), at(was), items.end());
  } else if(newCount < oldCount) {
    std::move(at(bandEnd), at(was), at(first + newCount));
    items.resize(was - (oldCount - newCount));
  }
}

// The same, with the band's new elements supplied. `incoming` is consumed by
// move where the iterators allow it, so a band of elements that own storage --
// an outline entry's heading text -- costs a pointer swap each and no
// allocation.
template <class Vector, class InputIt>
std::size_t replaceBand(Vector& items, std::size_t first, std::size_t oldCount, InputIt begin,
                        InputIt end) {
  const auto newCount = static_cast<std::size_t>(std::distance(begin, end));
  openBand(items, first, oldCount, newCount);
  using Diff = typename std::iterator_traits<decltype(items.begin())>::difference_type;
  std::move(begin, end, items.begin() + static_cast<Diff>(first));
  return newCount;
}

}
