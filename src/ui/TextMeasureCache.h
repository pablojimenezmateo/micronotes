#pragma once

#include "ui/Fonts.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// A bounded cache of measured text widths.
//
// Measuring a run means shaping it: `TTF_GetStringSize` walks the string,
// resolves each glyph and applies kerning, and it costs roughly a microsecond
// for a word. That is invisible until something asks for a whole document at
// once -- laying out a 460 KB note made 154,000 of those calls and spent about
// 120 ms of the 147 ms the layout took inside them, which is a third of a
// second of dead window between clicking a note and seeing it.
//
// The measurements repeat, heavily. Word frequency in prose is Zipfian: the few
// hundred commonest words are most of the running text, every space is the same
// space, and a scroll re-measures runs it measured on the last frame. This turns
// that repetition into a hash lookup.
//
// Direct-mapped rather than LRU. The texture cache next door needs an eviction
// policy because a wrong eviction costs a re-rasterisation and a texture upload;
// here a miss costs one shaping call, so the simplest possible policy -- the new
// entry takes the slot -- buys the whole win with one array, no allocation after
// construction, and one cache line touched per lookup.
//
// Keys are 64-bit hashes of the bytes and the style, and the full key is stored
// and compared, so a hit is a hit on the hash rather than on a truncated index.
// Two distinct runs colliding across a full 64-bit hash would return a wrong
// width; at the ~10^5 distinct runs a session sees, that is around one chance in
// three billion, and the texture cache beside it already takes the same bet for
// a worse outcome (wrong glyphs rather than a wrong advance).
//
// **Before enlarging it: the misses are compulsory, not conflict.** Measured
// on the harness's worst case -- `font.open_unique_words`, a 200 KB note in
// which no word repeats -- eight times the slots (2^16 to 2^19) moved the hit
// rate from 53.2% to 55.7% and peak RSS from 32.1 MB to 39.6 MB. A keystroke
// went from 97.0% to 99.2% and did not get faster, because 97% was already
// past the point where the shaping matters. The 47% that miss are words the
// cache has never seen, and no size fixes that.
class TextMeasureCache {
public:
  // Rounded up to a power of two, so the index is a mask rather than a modulo.
  explicit TextMeasureCache(std::size_t capacity);

  // The key for a run. Exposed so a caller can hash once and use the key for
  // both the lookup and the insert.
  static std::uint64_t makeKey(std::string_view text, const TextStyle& style, float displayScale);

  // Returns true and fills `width` on a hit.
  bool find(std::uint64_t key, int* width) const;
  void insert(std::uint64_t key, int width);

  // Every stored width was measured with the old faces. Called when they change.
  void clear();

private:
  struct Slot {
    std::uint64_t key = 0;
    int width = 0;
    bool used = false;
  };

  std::vector<Slot> slots_;
  std::size_t mask_ = 0;
};

}
