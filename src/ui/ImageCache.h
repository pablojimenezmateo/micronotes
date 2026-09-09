#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <map>

// The decoded pictures a note shows. Nothing else in the shell decodes a file,
// and nothing else holds VRAM by the megabyte, which is why it is a unit of its
// own rather than a class inside the drawing header.
namespace micronotes::ui {

// The decoded pictures a note shows.
//
// Two things are cached here and they have very different lifetimes. An
// image's **size** is what the layout reserves a box from, and it is the same
// number every time the file is decoded -- so it is learned once and kept, and
// `generation()` moves only when a *new* one is learned. Its **texture** is
// the expensive part, and it is evictable: dropping one costs a decode the next
// time the picture is drawn and changes no layout at all, which is what stops
// eviction from cascading into a relayout of the note.
//
// The budget is in **bytes, not entries**, because textures are not uniform:
// a note's 240x140 diagram is 134 KB and a phone photograph is 48 MB, and a cap
// counted in entries is either uselessly small for the first or fatal for the
// second. Eviction is least-recently-used, which is the policy
// `render::TextTextureCache` already settled on for the same reason -- the live
// set here cannot be enumerated from where the cache is asked.
class ImageCache {
public:
  explicit ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {}

  ~ImageCache() {
    clear();
  }

  ImageCache(const ImageCache&) = delete;
  ImageCache& operator=(const ImageCache&) = delete;

  // Everything, sizes included. The renderer going away under the cache is the
  // only reason to: an ordinary eviction drops a texture and keeps the size.
  void clear();

  // Moves whenever what this cache holds could change a layout -- which is when
  // a picture's size becomes known for the first time, and at no other moment.
  // A surface that memoises a layout containing an image keys on it: until the
  // file has been decoded there is no size, so the block is a caption one frame
  // and a picture the next.
  std::uint64_t generation() const {
    return generation_;
  }

  // The texture for `path`, decoding it if this is the first ask or if its
  // texture has since been evicted. `width` and `height` come back set whenever
  // the file has ever decoded, texture or no texture, because that is what the
  // layout needs and it does not change.
  SDL_Texture* load(const std::filesystem::path& path, float& width, float& height);

  // Resident texture bytes, for whoever wants to know what the budget is doing.
  std::uint64_t residentBytes() const {
    return bytes_;
  }

private:
  struct Entry {
    // Null while evicted, and null forever for a file that would not decode --
    // `undecodable` is which. Without that flag a broken link is an
    // `IMG_LoadTexture` attempt per relaid block, for the life of the note.
    SDL_Texture* texture = nullptr;
    float w = 0.0f;
    float h = 0.0f;
    bool measured = false;
    bool undecodable = false;
    // Tick of the last `load`, which is what least-recently-used means here.
    std::uint64_t used = 0;
  };

  void evict();

  SDL_Renderer* renderer_ = nullptr;
  // Keyed by the resolved path. Entries outlive their textures, so this grows
  // with the number of distinct pictures a session has seen rather than with
  // what it is holding -- 32 bytes each, against a texture's megabytes.
  std::map<std::string, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t tick_ = 0;
  std::uint64_t bytes_ = 0;
};

}
