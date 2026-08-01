#pragma once

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"

#include "ui/Fonts.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>
#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
};

inline bool contains(const Rect& rect, float x, float y) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

inline SDL_FRect sdlRect(const Rect& rect) {
  return {rect.x, rect.y, rect.w, rect.h};
}

inline SDL_Rect clipRect(const Rect& rect) {
  return {
    static_cast<int>(std::floor(rect.x)),
    static_cast<int>(std::floor(rect.y)),
    static_cast<int>(std::ceil(rect.w)),
    static_cast<int>(std::ceil(rect.h)),
  };
}

class ClipGuard {
public:
  ClipGuard(SDL_Renderer* renderer, Rect rect) : renderer_(renderer) {
    clip_ = clipRect(rect);
    SDL_SetRenderClipRect(renderer_, &clip_);
  }

  ~ClipGuard() {
    SDL_SetRenderClipRect(renderer_, nullptr);
  }

private:
  SDL_Renderer* renderer_ = nullptr;
  SDL_Rect clip_ {};
};

void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void hLine(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color);
void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor);
void drawSurface(SDL_Renderer* renderer, Rect rect);
void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot);
// A disclosure triangle, drawn rather than typeset: the UI face has no glyph
// for one, and a triangle assembled from lines stays crisp at any scale.
void drawDisclosure(SDL_Renderer* renderer, Rect box, bool open, SDL_Color color);

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

  int width(std::string_view value, const ui::TextStyle& style) const {
    if(value.empty()) return 0;
    perf::addCounter(perf::CounterId::RenderTextMeasureCalls);
    int w = 0;
    if(fonts_.measure(value, style, &w, nullptr)) return toLogical(w);
    return static_cast<int>(value.size() * 8);
  }

  void draw(std::string_view value, float x, float y, SDL_Color color, const ui::TextStyle& style) {
    if(value.empty()) return;
    const std::string text(value);
    x = std::round(x);
    y = std::round(y);
    if(fonts_.ready()) {
      const CachedText* cached = texture(text, color, style);
      if(!cached) return;
      const float scale = fonts_.displayScale();
      SDL_FRect dst {x, y, static_cast<float>(cached->w) / scale, static_cast<float>(cached->h) / scale};
      SDL_RenderTexture(renderer_, cached->texture, nullptr, &dst);
      return;
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x, y, text.c_str());
  }

  // Draws an emoji scaled to fit inside `box` and centred there. A colour emoji
  // font is one fixed bitmap strike that SDL_ttf cannot resize, so the glyph
  // arrives at 128 pixels whatever was asked for; scaling the rendered texture
  // is the only place its size can be honoured. Returns false when no emoji
  // face is installed, so the caller can draw its own mark instead of tofu.
  bool drawIcon(std::string_view value, Rect box, SDL_Color color) {
    if(value.empty() || !fonts_.ready() || box.w <= 0.0f || box.h <= 0.0f) return false;
    if(!fonts_.hasIconFont()) return false;
    const CachedText* cached = iconTexture(std::string(value), color);
    if(!cached || cached->w <= 0 || cached->h <= 0) return false;
    const float w = static_cast<float>(cached->w);
    const float h = static_cast<float>(cached->h);
    const float fit = std::min(box.w / w, box.h / h);
    SDL_FRect dst {
      std::round(box.x + (box.w - w * fit) / 2.0f),
      std::round(box.y + (box.h - h * fit) / 2.0f),
      w * fit,
      h * fit,
    };
    SDL_RenderTexture(renderer_, cached->texture, nullptr, &dst);
    return true;
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

  void draw(std::string_view value, float x, float y, SDL_Color color = theme().text, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) {
    draw(value, x, y, color, styleFor(heading, mono, strong, emphasis));
  }

  void clear() {
    for(auto& [_, texture] : cache_) SDL_DestroyTexture(texture.texture);
    cache_.clear();
  }

private:
  struct CachedText {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
  };

  const CachedText* iconTexture(const std::string& text, SDL_Color color) {
    if(cache_.size() > 4096) clear();
    const auto key = "icon:" + cacheKey(text, color, ui::TextStyle {});
    auto found = cache_.find(key);
    if(found == cache_.end()) {
      SDL_Surface* surface = fonts_.renderIcon(text, color);
      if(!surface) return nullptr;
      SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
      CachedText cached {created, surface->w, surface->h};
      SDL_DestroySurface(surface);
      if(!created) return nullptr;
      found = cache_.emplace(key, cached).first;
    }
    return &found->second;
  }

  const CachedText* texture(const std::string& text, SDL_Color color, const ui::TextStyle& style) {
    perf::addCounter(perf::CounterId::RenderTextCacheQueries);
    if(cache_.size() > 4096) {
      perf::addCounter(perf::CounterId::RenderTextCacheEvictions, cache_.size());
      clear();
    }
    const auto key = cacheKey(text, color, style);
    auto found = cache_.find(key);
    if(found != cache_.end()) {
      perf::addCounter(perf::CounterId::RenderTextCacheHits);
    } else {
      perf::addCounter(perf::CounterId::RenderTextRasterizations);
      SDL_Surface* surface = fonts_.render(text, style, color);
      if(!surface) return nullptr;
      SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
      CachedText cached {created, surface->w, surface->h};
      SDL_DestroySurface(surface);
      if(!created) return nullptr;
      found = cache_.emplace(key, cached).first;
    }
    return &found->second;
  }

  static std::string cacheKey(const std::string& text, SDL_Color color, const ui::TextStyle& style) {
    std::string key;
    key.reserve(text.size() + 24);
    key += std::to_string(color.r);
    key += ':';
    key += std::to_string(color.g);
    key += ':';
    key += std::to_string(color.b);
    key += ':';
    key += std::to_string(color.a);
    key += ':';
    key += style.family == ui::FontFamily::Mono ? 'm' : 's';
    key += style.strong ? 'b' : '-';
    key += style.italic ? 'i' : '-';
    key += ':';
    key += std::to_string(static_cast<int>(std::lround(style.size * 4.0f)));
    key += ':';
    key += text;
    return key;
  }

  SDL_Renderer* renderer_ = nullptr;
  ui::FontStore fonts_;
  std::map<std::string, CachedText> cache_;
};

class ImageCache {
public:
  explicit ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {}

  ~ImageCache() {
    clear();
  }

  void clear() {
    for(auto& [_, texture] : cache_) SDL_DestroyTexture(texture.texture);
    cache_.clear();
  }

  SDL_Texture* load(const std::filesystem::path& path, float& width, float& height) {
#if MICRONOTES_HAS_SDL3_IMAGE
    const auto key = path.string();
    auto found = cache_.find(key);
    if(found == cache_.end()) {
      SDL_Texture* texture = IMG_LoadTexture(renderer_, key.c_str());
      if(!texture) return nullptr;
      CachedImage image {texture, 0.0f, 0.0f};
      SDL_GetTextureSize(texture, &image.w, &image.h);
      if(cache_.size() > 512) clear();
      found = cache_.emplace(key, image).first;
    }
    width = found->second.w;
    height = found->second.h;
    return found->second.texture;
#else
    (void)path;
    (void)width;
    (void)height;
    return nullptr;
#endif
  }

private:
  struct CachedImage {
    SDL_Texture* texture = nullptr;
    float w = 0;
    float h = 0;
  };

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, CachedImage> cache_;
};

// Shortens `value` with an ellipsis until it fits `maxWidth`.
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style);
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading = false, bool mono = false);

}
