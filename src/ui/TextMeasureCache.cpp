#include "ui/TextMeasureCache.h"

#include "core/util/Hash.h"

#include <bit>
#include <cstring>

namespace micronotes::ui {
namespace {

using microcore::util::hashBytes;
using microcore::util::kFnvOffset;

}

TextMeasureCache::TextMeasureCache(std::size_t capacity) {
  const std::size_t size = std::bit_ceil(capacity < 2 ? std::size_t {2} : capacity);
  slots_.assign(size, Slot {});
  mask_ = size - 1;
}

std::uint64_t TextMeasureCache::makeKey(std::string_view text, const TextStyle& style, float displayScale) {
  std::uint64_t key = hashBytes(kFnvOffset, text.data(), text.size());
  // The style fields the measurement depends on, and the scale the faces were
  // built at: the same run at 1x and 2x is two different pixel widths.
  //
  // Packed into a fixed byte array rather than hashed as a struct. A struct of
  // {enum, bool, bool, float, float} is sixteen bytes with two of them padding
  // between `italic` and `size`, and aggregate initialisation leaves padding
  // *indeterminate* -- so `sizeof(fields)` bytes of it is two bytes of whatever
  // the stack held, mixed into a cache key. gcc at -O2 happens to zero them
  // today, which is why the hit rate looks fine; it is still reading
  // uninitialised memory, and the failure mode if a compiler stops doing that is
  // the same run hashing to two keys depending on what ran before it -- a
  // measure cache that silently stops hitting, with nothing to point at.
  //
  // Twelve bytes, every one of them written here, and no padding to reason
  // about.
  unsigned char fields[sizeof(std::uint32_t) + 2 + sizeof(float) * 2];
  const auto family = static_cast<std::uint32_t>(style.family);
  std::size_t at = 0;
  std::memcpy(fields + at, &family, sizeof(family));
  at += sizeof(family);
  fields[at++] = style.strong ? 1 : 0;
  fields[at++] = style.italic ? 1 : 0;
  std::memcpy(fields + at, &style.size, sizeof(style.size));
  at += sizeof(style.size);
  std::memcpy(fields + at, &displayScale, sizeof(displayScale));
  return hashBytes(key, fields, sizeof(fields));
}

bool TextMeasureCache::find(std::uint64_t key, int* width) const {
  const Slot& slot = slots_[key & mask_];
  if(!slot.used || slot.key != key) return false;
  if(width) *width = slot.width;
  return true;
}

void TextMeasureCache::insert(std::uint64_t key, int width) {
  slots_[key & mask_] = Slot {key, width, true};
}

void TextMeasureCache::clear() {
  slots_.assign(slots_.size(), Slot {});
}

}
