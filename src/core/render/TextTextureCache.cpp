#include "core/render/TextTextureCache.h"

#include "core/perf/PerformanceCounters.h"

namespace microcore::render {
namespace {

// FNV-1a. Cheap, no allocation, and good enough to key a texture cache: a
// collision would show the wrong glyph run, but at 64 bits over the few
// thousand live entries this cache holds, that is not a practical concern.
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

inline std::uint64_t fnvByte(std::uint64_t hash, unsigned char value) {
  return (hash ^ value) * kFnvPrime;
}

}

TextTextureCache::TextTextureCache(std::size_t capacity)
  : capacity_(capacity == 0 ? 1 : capacity) {}

TextTextureCache::~TextTextureCache() {
  clear();
}

TextTextureCache::Key TextTextureCache::makeKey(std::string_view text, SDL_Color color, Style style) {
  std::uint64_t hash = kFnvOffset;
  for(const char c : text) hash = fnvByte(hash, static_cast<unsigned char>(c));
  hash = fnvByte(hash, color.r);
  hash = fnvByte(hash, color.g);
  hash = fnvByte(hash, color.b);
  hash = fnvByte(hash, color.a);
  const auto styleBits = static_cast<unsigned char>(
    (style.heading ? 1 : 0) | (style.mono ? 2 : 0) | (style.strong ? 4 : 0) | (style.emphasis ? 8 : 0));
  hash = fnvByte(hash, styleBits);
  const auto size = static_cast<std::uint32_t>(style.size * 64.0f);
  for(int shift = 0; shift < 32; shift += 8) {
    hash = fnvByte(hash, static_cast<unsigned char>((size >> shift) & 0xFF));
  }
  return hash;
}

const TextTextureCache::Entry* TextTextureCache::find(std::string_view text, SDL_Color color, Style style) {
  perf::addCounter(perf::CounterId::RenderTextCacheQueries);
  const auto key = makeKey(text, color, style);
  const auto found = entries_.find(key);
  if(found == entries_.end()) return nullptr;
  perf::addCounter(perf::CounterId::RenderTextCacheHits);
  // Promote to most-recently-used. splice moves the node without reallocating,
  // so the stored iterator stays valid.
  order_.splice(order_.begin(), order_, found->second);
  return &found->second->entry;
}

const TextTextureCache::Entry* TextTextureCache::insert(std::string_view text, SDL_Color color,
                                                        Style style, Entry entry) {
  const auto key = makeKey(text, color, style);
  const auto existing = entries_.find(key);
  if(existing != entries_.end()) {
    // Same key rendered twice before the first insert landed; keep one texture.
    if(existing->second->entry.texture && existing->second->entry.texture != entry.texture) {
      SDL_DestroyTexture(existing->second->entry.texture);
    }
    existing->second->entry = entry;
    order_.splice(order_.begin(), order_, existing->second);
    return &existing->second->entry;
  }

  order_.push_front(Node {key, entry});
  entries_.emplace(key, order_.begin());

  // Evict one at a time from the cold end, rather than flushing everything.
  while(entries_.size() > capacity_) {
    const auto& victim = order_.back();
    if(victim.entry.texture) SDL_DestroyTexture(victim.entry.texture);
    entries_.erase(victim.key);
    order_.pop_back();
    perf::addCounter(perf::CounterId::RenderTextCacheEvictions);
  }

  return &order_.front().entry;
}

void TextTextureCache::clear() {
  for(auto& node : order_) {
    if(node.entry.texture) SDL_DestroyTexture(node.entry.texture);
  }
  order_.clear();
  entries_.clear();
}

}
