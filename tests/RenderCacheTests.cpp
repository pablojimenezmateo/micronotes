#include "TestSupport.h"

#include "core/render/FontResolver.h"
#include "core/render/TextTextureCache.h"

#include <filesystem>
#include <string>

// The two caches every drawn character goes through, and the resolver that
// finds the file behind a face.
//
// They lived in `ArchitectureTests.cpp`, which they were never about: nothing
// here reads the source tree or checks where anything lives. These are ordinary
// behaviour tests of `core/render/`, and the eviction one in particular is the
// kind that only fails under a workload -- a cache that throws everything away
// still answers every query correctly, it just answers them slowly.

// Hardcoded /usr/share/fonts/truetype/dejavu/ paths meant a machine without
// DejaVu installed exactly there rendered no text at all: TTF_OpenFont returned
// null and every draw call silently did nothing.
MICRONOTES_TEST(font_resolver_finds_a_real_file_for_every_face) {
  const microcore::render::FontRequest requests[] = {
    {false, false, false}, {false, true, false}, {false, false, true},
    {false, true, true},   {true, false, false},
  };
  for(const auto& request : requests) {
    const auto path = microcore::render::resolveFontFile(request);
    micronotes::tests::require(!path.empty(), "no font resolved for a requested face");
    micronotes::tests::require(std::filesystem::exists(path), "resolved font does not exist: " + path);
  }
}

// The old cache destroyed every texture on reaching 4096 entries. Besides the
// one-frame cost of thousands of destructions, a full flush discards exactly
// the entries about to be reused, because it cannot tell which those are.
MICRONOTES_TEST(text_texture_cache_evicts_least_recently_used_not_everything) {
  microcore::render::TextTextureCache cache(3);
  const SDL_Color color {255, 255, 255, 255};
  const microcore::render::TextTextureCache::Style style {};
  // Null textures: the cache only ever destroys a non-null one, so this
  // exercises the eviction policy without needing a live renderer.
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};
  const auto key = [&](std::string_view text) {
    return microcore::render::TextTextureCache::makeKey(text, color, style);
  };

  cache.insert(key("a"), entry);
  cache.insert(key("b"), entry);
  cache.insert(key("c"), entry);
  MICRONOTES_REQUIRE(cache.size() == 3);

  // Touch "a" so "b" becomes the coldest entry.
  MICRONOTES_REQUIRE(cache.find(key("a")) != nullptr);
  cache.insert(key("d"), entry);

  MICRONOTES_REQUIRE(cache.size() == 3);
  MICRONOTES_REQUIRE(cache.find(key("b")) == nullptr);  // evicted
  MICRONOTES_REQUIRE(cache.find(key("a")) != nullptr);  // kept: recently used
  MICRONOTES_REQUIRE(cache.find(key("c")) != nullptr);
  MICRONOTES_REQUIRE(cache.find(key("d")) != nullptr);
}

MICRONOTES_TEST(text_texture_cache_distinguishes_colour_and_style) {
  microcore::render::TextTextureCache cache(16);
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color red {255, 0, 0, 255};

  using Cache = microcore::render::TextTextureCache;
  cache.insert(Cache::makeKey("x", white, {}), entry);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", red, {})) == nullptr);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", white, {true, false, false, false})) == nullptr);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", white, {})) != nullptr);
}
