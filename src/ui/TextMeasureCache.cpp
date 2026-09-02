#include "ui/TextMeasureCache.h"

#include <bit>
#include <cstring>

namespace micronotes::ui {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t hashBytes(std::uint64_t seed, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t i = 0;
  for(; i + sizeof(std::uint64_t) <= size; i += sizeof(std::uint64_t)) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes + i, sizeof(word));
    seed = (seed ^ word) * kFnvPrime;
    seed ^= seed >> 29;
  }
  for(; i < size; ++i) {
    seed = (seed ^ bytes[i]) * kFnvPrime;
  }
  return seed;
}

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
  const struct {
    FontFamily family;
    bool strong;
    bool italic;
    float size;
    float scale;
  } fields {style.family, style.strong, style.italic, style.size, displayScale};
  return hashBytes(key, &fields, sizeof(fields));
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
