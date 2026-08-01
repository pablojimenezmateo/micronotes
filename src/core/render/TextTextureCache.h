#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace microcore::render {

// A bounded, least-recently-used cache of rasterized text runs.
//
// It replaces a std::map that, on reaching 4096 entries, destroyed *every*
// texture at once and started again. That has two costs. The obvious one is the
// cliff: one frame pays for thousands of texture destructions and then
// re-rasterizes everything still on screen, which is a visible hitch rather
// than a gradual cost. The subtler one is that a full flush discards exactly
// the entries that were about to be used again -- the visible ones -- because
// it has no idea which those are. Evicting the least recently used entry
// instead keeps the working set resident and makes the steady state flat.
//
// The old key was also built by concatenating the text with six style fields
// into a std::string on *every lookup*, so a cache hit still allocated and then
// compared strings down a red-black tree. Hashing the same fields into a 64-bit
// key removes the allocation and the string compares.
class TextTextureCache {
public:
  struct Entry {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
  };

  // Style bits folded into the cache key. A run rendered bold is a different
  // texture from the same run rendered regular.
  struct Style {
    bool heading = false;
    bool mono = false;
    bool strong = false;
    bool emphasis = false;
    // Callers that size text freely (rather than by a heading flag) need the
    // size in the key: the same run at 13px and 20px is two textures.
    float size = 0.0f;
  };

  explicit TextTextureCache(std::size_t capacity);
  ~TextTextureCache();

  TextTextureCache(const TextTextureCache&) = delete;
  TextTextureCache& operator=(const TextTextureCache&) = delete;

  // Returns the cached entry, or nullptr when absent. On a hit the entry is
  // promoted to most-recently-used.
  const Entry* find(std::string_view text, SDL_Color color, Style style);

  // Takes ownership of `texture`. Evicts the least recently used entry when
  // over capacity.
  const Entry* insert(std::string_view text, SDL_Color color, Style style, Entry entry);

  // Destroys every texture. Used when the font changes, which invalidates all
  // of them at once -- the one case a full flush is actually correct.
  void clear();

  std::size_t size() const { return entries_.size(); }

private:
  using Key = std::uint64_t;

  static Key makeKey(std::string_view text, SDL_Color color, Style style);

  struct Node {
    Key key;
    Entry entry;
  };

  std::size_t capacity_;
  // Front is most-recently-used. A list keeps promotion and eviction O(1) and
  // keeps iterators stable, so the map can point straight at the node.
  std::list<Node> order_;
  std::unordered_map<Key, std::list<Node>::iterator> entries_;
};

}
