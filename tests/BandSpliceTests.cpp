#include "TestSupport.h"

#include "core/util/BandSplice.h"

#include <memory>
#include <string>
#include <vector>

using microcore::util::openBand;
using microcore::util::replaceBand;

namespace {

std::vector<int> spliced(std::vector<int> items, std::size_t first, std::size_t oldCount,
                         const std::vector<int>& incoming) {
  auto copy = incoming;
  replaceBand(items, first, oldCount, copy.begin(), copy.end());
  return items;
}

}

MICRONOTES_TEST(band_splice_replaces_a_band_in_place) {
  const std::vector<int> base {0, 1, 2, 3, 4, 5};
  // Same size: nothing moves at all, which is the common case -- a typed
  // character rewraps one line into the same number of rows.
  MICRONOTES_REQUIRE((spliced(base, 2, 2, {20, 30}) == std::vector<int> {0, 1, 20, 30, 4, 5}));
  // Grows.
  MICRONOTES_REQUIRE(
    (spliced(base, 2, 2, {20, 30, 40}) == std::vector<int> {0, 1, 20, 30, 40, 4, 5}));
  // Shrinks.
  MICRONOTES_REQUIRE((spliced(base, 2, 2, {20}) == std::vector<int> {0, 1, 20, 4, 5}));
  // Empties -- a keystroke inside a paragraph removes every heading the middle
  // had and adds none, which is nearly every keystroke the outline sees.
  MICRONOTES_REQUIRE((spliced(base, 2, 2, {}) == std::vector<int> {0, 1, 4, 5}));
}

MICRONOTES_TEST(band_splice_handles_the_ends_and_the_whole) {
  const std::vector<int> base {0, 1, 2, 3};
  // A band at the front, with nothing before it.
  MICRONOTES_REQUIRE((spliced(base, 0, 2, {9}) == std::vector<int> {9, 2, 3}));
  // A band at the back, with nothing after it -- no tail to move.
  MICRONOTES_REQUIRE((spliced(base, 2, 2, {9, 9, 9}) == std::vector<int> {0, 1, 9, 9, 9}));
  // The whole vector.
  MICRONOTES_REQUIRE((spliced(base, 0, 4, {7}) == std::vector<int> {7}));
  // An insertion that removes nothing.
  MICRONOTES_REQUIRE((spliced(base, 2, 0, {8, 8}) == std::vector<int> {0, 1, 8, 8, 2, 3}));
  // A removal that inserts nothing.
  MICRONOTES_REQUIRE((spliced(base, 1, 2, {}) == std::vector<int> {0, 3}));
  // An empty vector, which a caller reaches by asking for a band of nothing at
  // its only valid index.
  MICRONOTES_REQUIRE((spliced({}, 0, 0, {1, 2}) == std::vector<int> {1, 2}));
  MICRONOTES_REQUIRE((spliced(base, 1, 0, {}) == base));
}

MICRONOTES_TEST(band_splice_moves_the_tail_rather_than_copying_it) {
  // The property the whole thing is for. A tail element that owns storage is
  // carried by move, so the splice costs a pointer swap per tail element and
  // no allocation -- which is what `erase` plus `insert` also does, and what
  // copying the tail out and back does not.
  std::vector<std::unique_ptr<int>> items;
  for(int value = 0; value < 6; ++value) items.push_back(std::make_unique<int>(value));
  const int* tailAddress = items[4].get();

  std::vector<std::unique_ptr<int>> incoming;
  incoming.push_back(std::make_unique<int>(90));
  replaceBand(items, 2, 2, std::make_move_iterator(incoming.begin()),
              std::make_move_iterator(incoming.end()));

  MICRONOTES_REQUIRE(items.size() == 5);
  MICRONOTES_REQUIRE(*items[2] == 90);
  // The tail element is the same object, not a copy of it.
  MICRONOTES_REQUIRE(items[3].get() == tailAddress);
  MICRONOTES_REQUIRE(*items[4] == 5);
}

MICRONOTES_TEST(band_splice_leaves_room_without_writing_it) {
  // `openBand` is the form the layout's parallel arrays need: the band's
  // contents are written by a later walk, so all this has to do is get the
  // indexing right.
  std::vector<std::string> items {"a", "b", "c", "d"};
  openBand(items, 1, 1, 3);
  MICRONOTES_REQUIRE(items.size() == 6);
  MICRONOTES_REQUIRE(items[0] == "a");
  MICRONOTES_REQUIRE(items[4] == "c");
  MICRONOTES_REQUIRE(items[5] == "d");

  openBand(items, 1, 3, 0);
  MICRONOTES_REQUIRE(items.size() == 3);
  MICRONOTES_REQUIRE((items == std::vector<std::string> {"a", "c", "d"}));
}
