#include "core/pdf/SfntKern.h"

#include "core/pdf/SfntRead.h"

#include <algorithm>

namespace microcore::pdf {
namespace {

using sfnt::s16At;
using sfnt::u16At;
using sfnt::u32At;

// The four `ValueRecord` bits that are a signed value each, in the order the
// format writes them. `XAdvance` is the fourth field, and its offset inside a
// record depends on which of the ones before it are present -- which is what
// makes reading one a popcount rather than a fixed offset.
constexpr std::uint16_t kXPlacement = 0x0001;
constexpr std::uint16_t kYPlacement = 0x0002;
constexpr std::uint16_t kXAdvance = 0x0004;

// How many bytes a `ValueRecord` of this format occupies. Every set bit is one
// 16-bit field, whether it is a value or a device-table offset.
std::size_t valueRecordSize(std::uint16_t format) {
  std::size_t fields = 0;
  for(std::uint16_t bit = 1; bit != 0; bit = static_cast<std::uint16_t>(bit << 1)) {
    if((format & bit) != 0) ++fields;
  }
  return fields * 2;
}

// The `XAdvance` in a `ValueRecord` at `at`, or zero when the format does not
// carry one. Only the first glyph's advance matters for kerning: the second
// glyph's own advance is its own, and a `TJ` array adjusts the gap between
// them, which is the same thing said once.
int xAdvanceIn(std::string_view gpos, std::size_t at, std::uint16_t format) {
  if((format & kXAdvance) == 0) return 0;
  std::size_t offset = at;
  if((format & kXPlacement) != 0) offset += 2;
  if((format & kYPlacement) != 0) offset += 2;
  return s16At(gpos, offset);
}

constexpr std::size_t kNoRecord = static_cast<std::size_t>(-1);

// Binary search over `count` fixed-size records starting at `first`, each led
// by the glyph id it is about. Returns the record's own offset, or `kNoRecord`.
//
// Two of the three tables read here are exactly that and differ only in their
// stride: a format 1 coverage table is bare glyph ids (stride 2) and a
// `PairSet`'s entries are a glyph id followed by one or two `ValueRecord`s
// (stride computed from the value formats). Sorted is the format's guarantee,
// and a search rather than a walk matters because a coverage table on a Latin
// face runs to several hundred glyphs and this is asked once per pair.
std::size_t recordFor(std::string_view gpos, std::size_t first, std::size_t count,
                      std::size_t stride, std::uint16_t glyph) {
  std::size_t low = 0;
  std::size_t high = count;
  while(low < high) {
    const std::size_t mid = (low + high) / 2;
    const std::size_t record = first + mid * stride;
    const std::uint16_t candidate = u16At(gpos, record);
    if(candidate == glyph) return record;
    if(candidate < glyph) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return kNoRecord;
}

// The `RangeRecord` array that *both* format 2 tables here are: a count at
// `at + 2`, then six-byte records of {first glyph, last glyph, value}, sorted
// and disjoint. Fills `first` and `value` and returns whether the glyph is in
// one.
//
// The two readers take the value differently -- a coverage table's is the
// index its first glyph has, so the answer is that plus the offset into the
// range, and a class definition's is the class itself. The *search* is the
// same, and was written out twice.
bool rangeRecordFor(std::string_view gpos, std::size_t at, std::uint16_t glyph,
                    std::uint16_t* first, std::uint16_t* value) {
  const std::uint16_t ranges = u16At(gpos, at + 2);
  std::size_t low = 0;
  std::size_t high = ranges;
  while(low < high) {
    const std::size_t mid = (low + high) / 2;
    const std::size_t record = at + 4 + mid * 6;
    const std::uint16_t start = u16At(gpos, record);
    const std::uint16_t end = u16At(gpos, record + 2);
    if(glyph < start) {
      high = mid;
    } else if(glyph > end) {
      low = mid + 1;
    } else {
      *first = start;
      *value = u16At(gpos, record + 4);
      return true;
    }
  }
  return false;
}

// Where `glyph` sits in a coverage table, or -1 when it is not covered.
int coverageIndex(std::string_view gpos, std::size_t at, std::uint16_t glyph) {
  const std::uint16_t format = u16At(gpos, at);
  if(format == 1) {
    const std::size_t record = recordFor(gpos, at + 4, u16At(gpos, at + 2), 2, glyph);
    if(record == kNoRecord) return -1;
    return static_cast<int>((record - (at + 4)) / 2);
  }
  if(format != 2) return -1;
  std::uint16_t start = 0;
  std::uint16_t index = 0;
  if(!rangeRecordFor(gpos, at, glyph, &start, &index)) return -1;
  return static_cast<int>(index) + (glyph - start);
}

// Which class a glyph is in. Class 0 is "everything not mentioned", which is
// both the format's rule and a real class a kerning matrix has a row for.
std::uint16_t classOf(std::string_view gpos, std::size_t at, std::uint16_t glyph) {
  const std::uint16_t format = u16At(gpos, at);
  if(format == 1) {
    const std::uint16_t start = u16At(gpos, at + 2);
    const std::uint16_t count = u16At(gpos, at + 4);
    if(glyph < start || glyph >= start + count) return 0;
    return u16At(gpos, at + 6 + static_cast<std::size_t>(glyph - start) * 2);
  }
  if(format != 2) return 0;
  std::uint16_t start = 0;
  std::uint16_t which = 0;
  if(!rangeRecordFor(gpos, at, glyph, &start, &which)) return 0;
  return which;
}

// One `PairPos` subtable, format 1: the pairs written out one by one, grouped
// by their first glyph.
bool pairFromFormat1(std::string_view gpos, std::size_t at, std::uint16_t left,
                     std::uint16_t right, int* out) {
  const int index = coverageIndex(gpos, at + u16At(gpos, at + 2), left);
  if(index < 0) return false;
  const std::uint16_t valueFormat1 = u16At(gpos, at + 4);
  const std::uint16_t valueFormat2 = u16At(gpos, at + 6);
  const std::uint16_t pairSets = u16At(gpos, at + 8);
  if(index >= static_cast<int>(pairSets)) return false;
  const std::size_t set =
    at + u16At(gpos, at + 10 + static_cast<std::size_t>(index) * 2);
  const std::uint16_t pairs = u16At(gpos, set);
  const std::size_t stride = 2 + valueRecordSize(valueFormat1) + valueRecordSize(valueFormat2);
  // Sorted by the second glyph, which the format guarantees.
  const std::size_t record = recordFor(gpos, set + 2, pairs, stride, right);
  if(record == kNoRecord) return false;
  *out = xAdvanceIn(gpos, record + 2, valueFormat1);
  return true;
}

// The same subtable, format 2: a matrix over two class definitions, which is
// how a face kerns every round letter against every straight one without
// writing the cross product out.
bool pairFromFormat2(std::string_view gpos, std::size_t at, std::uint16_t left,
                     std::uint16_t right, int* out) {
  // The coverage still gates it: a glyph outside it is not kerned by this
  // subtable even if its class has a row. Fonts rely on that -- class 0 is
  // "everything else", and without the coverage check every glyph in the font
  // would take class 0's row.
  if(coverageIndex(gpos, at + u16At(gpos, at + 2), left) < 0) return false;
  const std::uint16_t valueFormat1 = u16At(gpos, at + 4);
  const std::uint16_t valueFormat2 = u16At(gpos, at + 6);
  const std::uint16_t class1Count = u16At(gpos, at + 12);
  const std::uint16_t class2Count = u16At(gpos, at + 14);
  const std::uint16_t first = classOf(gpos, at + u16At(gpos, at + 8), left);
  const std::uint16_t second = classOf(gpos, at + u16At(gpos, at + 10), right);
  if(first >= class1Count || second >= class2Count) return false;
  const std::size_t recordSize = valueRecordSize(valueFormat1) + valueRecordSize(valueFormat2);
  const std::size_t record = at + 16 +
                             (static_cast<std::size_t>(first) * class2Count +
                              static_cast<std::size_t>(second)) *
                               recordSize;
  *out = xAdvanceIn(gpos, record, valueFormat1);
  return true;
}

// A lookup's subtables, added to `into` when it is a pair adjustment -- through
// an extension lookup if that is how the font wrapped it. Extension is not a
// rarity: a face whose GPOS runs past 64 KB has to use it, and Inter does.
void collectLookup(std::string_view gpos, std::size_t at, std::vector<std::uint32_t>& into) {
  const std::uint16_t type = u16At(gpos, at);
  const std::uint16_t count = u16At(gpos, at + 4);
  for(std::uint16_t i = 0; i < count; ++i) {
    const std::uint32_t subtable = at + u16At(gpos, at + 6 + static_cast<std::size_t>(i) * 2);
    if(type == 2) {
      into.push_back(subtable);
      continue;
    }
    if(type != 9) continue;
    // `ExtensionPosLookup`: a format, the type it stands in for, and a 32-bit
    // offset from the extension subtable's own start.
    if(u16At(gpos, subtable) != 1) continue;
    if(u16At(gpos, subtable + 2) != 2) continue;
    into.push_back(subtable + u32At(gpos, subtable + 4));
  }
}

}

void KernPairs::parse(std::string_view gpos) {
  subtables_.clear();
  if(gpos.size() < 10) return;
  const std::uint32_t version = u32At(gpos, 0);
  if((version >> 16) != 1) return;
  const std::uint16_t featureListAt = u16At(gpos, 6);
  const std::uint16_t lookupListAt = u16At(gpos, 8);
  if(featureListAt == 0 || lookupListAt == 0) return;

  const std::uint16_t lookupCount = u16At(gpos, lookupListAt);
  const std::uint16_t featureCount = u16At(gpos, featureListAt);
  for(std::uint16_t i = 0; i < featureCount; ++i) {
    const std::size_t record = featureListAt + 2 + static_cast<std::size_t>(i) * 6;
    // 'kern'. Every feature the union of the scripts names, rather than the
    // ones a particular script's language system selects: a note is not tagged
    // with a script, the shaping above this is one run of glyphs with no
    // language attached, and for Latin text every `kern` feature in a face is
    // the same lookups. Reading the script list to reach the same answer would
    // be a page of code that never changes it.
    if(u32At(gpos, record) != 0x6B65726Eu) continue;
    const std::size_t feature = featureListAt + u16At(gpos, record + 4);
    const std::uint16_t indices = u16At(gpos, feature + 2);
    for(std::uint16_t j = 0; j < indices; ++j) {
      const std::uint16_t index = u16At(gpos, feature + 4 + static_cast<std::size_t>(j) * 2);
      if(index >= lookupCount) continue;
      const std::size_t lookup =
        lookupListAt + u16At(gpos, lookupListAt + 2 + static_cast<std::size_t>(index) * 2);
      collectLookup(gpos, lookup, subtables_);
    }
  }
  // A feature named by two scripts is one feature, and its lookups would
  // otherwise be walked twice per pair for the same answer.
  std::sort(subtables_.begin(), subtables_.end());
  subtables_.erase(std::unique(subtables_.begin(), subtables_.end()), subtables_.end());
}

int KernPairs::adjustment(std::string_view gpos, std::uint16_t left, std::uint16_t right) const {
  for(const std::uint32_t at : subtables_) {
    int adjustment = 0;
    const std::uint16_t format = u16At(gpos, at);
    const bool found = format == 1 ? pairFromFormat1(gpos, at, left, right, &adjustment)
                     : format == 2 ? pairFromFormat2(gpos, at, left, right, &adjustment)
                                   : false;
    // The first subtable that *covers* the pair decides it, kerned or not.
    // Falling through to the next one would let a class matrix that says "no
    // adjustment for this pair" be overridden by a later, coarser rule.
    if(found) return adjustment;
  }
  return 0;
}

}
