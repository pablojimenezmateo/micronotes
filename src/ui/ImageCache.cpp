#include "ui/ImageCache.h"
#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"

#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include <algorithm>
#include <utility>
#include <vector>

namespace micronotes::ui {

// How much VRAM the decoded pictures of a note may hold between them.
//
// A number of bytes rather than of entries. The cap this replaced was 512
// entries, which for a note of 240x140 diagrams is 69 MB and for one of phone
// photographs is 24 GB -- the same cap meaning two things four orders of
// magnitude apart, because it counted the wrong thing.
constexpr std::uint64_t kImageBudgetBytes = 192ull * 1024ull * 1024ull;

// SDL keeps a texture as 32-bit pixels whatever the file was, so this is the
// size of the decode rather than of the file on disk.
std::uint64_t textureBytes(float w, float h) {
  if(w <= 0.0f || h <= 0.0f) return 0;
  return static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4ull;
}

void ImageCache::clear() {
  for(auto& [_, entry] : entries_) SDL_DestroyTexture(entry.texture);
  entries_.clear();
  bytes_ = 0;
  ++generation_;
}

// Least-recently-used, down to the budget, and never the entry that was just
// asked for: `load` stamps it with the newest tick before calling here, so
// ascending order reaches it last and the loop has stopped by then.
//
// The sizes stay. That is the whole point of evicting here rather than erasing:
// a layout that reserved a box for this picture keeps the box, so dropping a
// texture costs a decode the next time it is drawn and does not move
// `generation()` -- where erasing the size would move it twice, once to forget
// and once to learn again, and each move relays out the note.
void ImageCache::evict() {
  if(bytes_ <= kImageBudgetBytes) return;
  std::vector<std::pair<std::uint64_t, Entry*>> resident;
  resident.reserve(entries_.size());
  for(auto& [_, entry] : entries_) {
    if(entry.texture) resident.emplace_back(entry.used, &entry);
  }
  std::sort(resident.begin(), resident.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for(const auto& [_, entry] : resident) {
    if(bytes_ <= kImageBudgetBytes || resident.size() == 1) break;
    SDL_DestroyTexture(entry->texture);
    entry->texture = nullptr;
    bytes_ -= textureBytes(entry->w, entry->h);
    perf::addCounter(perf::CounterId::ImageTexturesEvicted);
  }
}

SDL_Texture* ImageCache::load(const std::filesystem::path& path, float& width, float& height) {
#if MICRONOTES_HAS_SDL3_IMAGE
  const auto key = path.string();
  Entry& entry = entries_[key];
  entry.used = ++tick_;
  if(entry.texture) {
    perf::addCounter(perf::CounterId::ImageTextureCacheHits);
  } else if(!entry.undecodable) {
    if(SDL_Texture* texture = IMG_LoadTexture(renderer_, key.c_str())) {
      perf::addCounter(perf::CounterId::ImageTexturesLoaded);
      entry.texture = texture;
      float w = 0.0f;
      float h = 0.0f;
      SDL_GetTextureSize(texture, &w, &h);
      // A file decodes to the same size every time, so this is learned once.
      // Moving the generation on a re-decode would tell every surface holding a
      // layout that an answer had changed when none had.
      if(!entry.measured) {
        entry.w = w;
        entry.h = h;
        entry.measured = true;
        ++generation_;
      }
      bytes_ += textureBytes(entry.w, entry.h);
      evict();
    } else {
      // Remembered, or a broken link is a decode attempt per relaid block for
      // the life of the note.
      entry.undecodable = true;
    }
  }
  width = entry.w;
  height = entry.h;
  return entry.texture;
#else
  (void)path;
  (void)width;
  (void)height;
  return nullptr;
#endif
}

}
