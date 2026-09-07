#include "core/render/TextTextureCache.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/Hash.h"

#include <cstring>

namespace microcore::render {
namespace {

// FNV-1a from `core/util/Hash.h`. Cheap, no allocation, and good enough to key
// a texture cache: a collision would show the wrong glyph run, but at 64 bits
// over the few thousand live entries this cache holds, that is not a practical
// concern.
using util::hashBytes;
using util::kFnvOffset;

}

TextTextureCache::TextTextureCache(std::size_t capacity)
  : capacity_(capacity == 0 ? 1 : capacity) {}

TextTextureCache::~TextTextureCache() {
  clear();
}

TextTextureCache::Key TextTextureCache::makeKey(std::string_view text, SDL_Color color, Style style) {
  // The text a word at a time rather than a byte at a time. This runs once per
  // drawn run of every frame, which makes it the hottest hash in the tree; it
  // was the one copy of FNV-1a that never picked up the wide loop.
  std::uint64_t hash = hashBytes(kFnvOffset, text);
  // The colour and the style packed into a fixed byte array and hashed in one
  // pass, every byte of it written here. Hashing the `Style` struct directly
  // would mix in the padding between `emphasis` and `size`, which aggregate
  // initialisation leaves indeterminate -- the same run would then key two ways
  // depending on what the stack held, and the cache would quietly stop hitting.
  unsigned char fields[4 + 1 + sizeof(std::uint32_t)];
  fields[0] = color.r;
  fields[1] = color.g;
  fields[2] = color.b;
  fields[3] = color.a;
  fields[4] = static_cast<unsigned char>((style.heading ? 1 : 0) | (style.mono ? 2 : 0) |
                                         (style.strong ? 4 : 0) | (style.emphasis ? 8 : 0));
  // Quantised rather than hashed as a float, so two sizes a rounding error
  // apart share a texture instead of each rasterizing their own.
  const auto size = static_cast<std::uint32_t>(style.size * 64.0f);
  std::memcpy(fields + 5, &size, sizeof(size));
  return hashBytes(hash, fields, sizeof(fields));
}

const TextTextureCache::Entry* TextTextureCache::find(Key key) {
  perf::addCounter(perf::CounterId::RenderTextCacheQueries);
  const auto found = entries_.find(key);
  if(found == entries_.end()) return nullptr;
  perf::addCounter(perf::CounterId::RenderTextCacheHits);
  // Promote to most-recently-used. splice moves the node without reallocating,
  // so the stored iterator stays valid.
  order_.splice(order_.begin(), order_, found->second);
  return &found->second->entry;
}

const TextTextureCache::Entry* TextTextureCache::insert(Key key, Entry entry) {
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
