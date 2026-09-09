#pragma once

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/render/TextTextureCache.h"

#include "ui/Fonts.h"
#include "ui/Rect.h"
#include "ui/TextMeasureCache.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

// Typesetting, and the two caches that make it affordable: a raster cache of
// drawn runs and a much larger cache of measured widths.
//
// This is the one thing nearly every surface in the shell wants, which is most
// of the argument for it having a name. While it lived in `ui/Draw.h`, a file
// that only needed to measure a string took the glyph table, the scrollbar, the
// menu row and the image decoder along with it.
namespace micronotes::ui {

class TextRenderer;

class TextRenderer {
public:
  explicit TextRenderer(SDL_Renderer* renderer) : renderer_(renderer) {
    fonts_.init();
  }

  ~TextRenderer() {
    clear();
    fonts_.shutdown();
  }

  ui::FontStore& fonts() {
    return fonts_;
  }

  // Rebuilding faces invalidates every cached glyph texture.
  void setDisplayScale(float scale) {
    if(std::abs(scale - fonts_.displayScale()) < 0.01f) return;
    fonts_.setDisplayScale(scale);
    clear();
  }

  float displayScale() const {
    return fonts_.displayScale();
  }

  // Glyphs are rasterized at physical pixels so they stay crisp, but every
  // caller lays out in logical units, so measurements convert back.
  int toLogical(int physical) const {
    const float scale = fonts_.displayScale();
    if(scale <= 1.001f && scale >= 0.999f) return physical;
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(physical) / scale)));
  }

  int lineHeight(const ui::TextStyle& style) const {
    const int height = fonts_.lineHeight(style);
    return height > 0 ? toLogical(height) + 2 : 16;
  }

  // Measured in physical pixels and cached there, so the logical conversion --
  // which depends only on the scale already in the key -- stays outside the
  // cache and the same entry serves every caller.
  int width(std::string_view value, const ui::TextStyle& style) const {
    if(value.empty()) return 0;
    perf::addCounter(perf::CounterId::RenderTextMeasureCalls);
    const std::uint64_t key = ui::TextMeasureCache::makeKey(value, style, fonts_.displayScale());
    int w = 0;
    if(measures_.find(key, &w)) {
      perf::addCounter(perf::CounterId::RenderTextMeasureCacheHits);
      return toLogical(w);
    }
    if(!fonts_.measure(value, style, &w, nullptr)) return static_cast<int>(value.size() * 8);
    measures_.insert(key, w);
    return toLogical(w);
  }

  void draw(std::string_view value, float x, float y, SDL_Color color, const ui::TextStyle& style) {
    if(value.empty()) return;
    x = std::round(x);
    y = std::round(y);
    if(fonts_.ready()) {
      // No `std::string` here. Every cache below this point is keyed on a hash
      // of the bytes and takes a view, and the rasterizer takes a view too --
      // so materialising one cost an allocation per drawn run per frame, on the
      // path that runs for every word on screen, purely to be looked up and
      // thrown away. Only the no-font fallback needs a terminator.
      const CachedText* cached = texture(value, color, style);
      if(!cached) return;
      const float scale = fonts_.displayScale();
      SDL_FRect dst {x, y, static_cast<float>(cached->w) / scale, static_cast<float>(cached->h) / scale};
      SDL_RenderTexture(renderer_, cached->texture, nullptr, &dst);
      return;
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x, y, std::string(value).c_str());
  }

  // Compatibility shims for the boolean-flag call sites inherited from the
  // pre-token UI. New code should pass a ui::TextStyle directly.
  static ui::TextStyle styleFor(bool heading, bool mono, bool strong, bool emphasis) {
    ui::TextStyle style;
    style.family = mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
    style.strong = strong || heading;
    style.italic = emphasis;
    style.size = heading ? ui::type().h2 : (mono ? ui::type().mono : ui::type().ui);
    return style;
  }

  int lineHeight(bool heading = false) const {
    return lineHeight(styleFor(heading, false, false, false));
  }

  int width(std::string_view value, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) const {
    return width(value, styleFor(heading, mono, strong, emphasis));
  }

  void draw(std::string_view value, float x, float y, SDL_Color color = theme().textPrimary, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) {
    draw(value, x, y, color, styleFor(heading, mono, strong, emphasis));
  }

  void clear() {
    cache_.clear();
    // Every stored width was measured with the faces being replaced.
    measures_.clear();
  }

private:
  using CachedText = render::TextTextureCache::Entry;

  static render::TextTextureCache::Style cacheStyle(const ui::TextStyle& style) {
    return render::TextTextureCache::Style {
      false, style.family == ui::FontFamily::Mono, style.strong, style.italic, style.size,
    };
  }

  const CachedText* texture(std::string_view text, SDL_Color color, const ui::TextStyle& style) {
    // Hashed once and used for both the probe and the insert below.
    const auto key = render::TextTextureCache::makeKey(text, color, cacheStyle(style));
    if(const auto* hit = cache_.find(key)) return hit;
    perf::addCounter(perf::CounterId::RenderTextRasterizations);
    SDL_Surface* surface = fonts_.render(text, style, color);
    if(!surface) return nullptr;
    SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
    CachedText entry {created, surface->w, surface->h};
    SDL_DestroySurface(surface);
    if(!created) return nullptr;
    return cache_.insert(key, entry);
  }


  SDL_Renderer* renderer_ = nullptr;
  ui::FontStore fonts_;
  // Sized for a few full screens of text so the working set stays resident
  // while scrolling.
  render::TextTextureCache cache_ {4096};
  // Widths are asked for far more often than textures -- once per word of a
  // layout pass against once per drawn run -- and an entry is 24 bytes rather
  // than a texture, so this is sized an order of magnitude larger. Mutable
  // because measuring is a query: a caller asking how wide a string is has not
  // changed anything a caller can observe.
  mutable ui::TextMeasureCache measures_ {1 << 16};
};

// The y one line of `style` is drawn at to sit centred in `row`.
//
// Row heights are chrome and stay put; text grows with the reader's text size.
// The gap above a line is therefore not a constant -- and it was written as one
// at a dozen call sites, each nudged by eye at the medium size, which is why the
// same list had rows at +3, +4 and +5 and why the large size drew a row's text
// over the row beneath it. Never negative: a line taller than the row it is in
// starts at the top and is clipped at the bottom rather than above.
inline float textTop(Rect row, const TextRenderer& text, const TextStyle& style) {
  return std::round(row.y + std::max(0.0f, (row.h - static_cast<float>(text.lineHeight(style))) / 2.0f));
}

// Shortens `value` with an ellipsis until it fits `maxWidth`.
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style);
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading = false, bool mono = false);

}
