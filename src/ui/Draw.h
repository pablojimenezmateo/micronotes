#pragma once

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/render/TextTextureCache.h"

#include "ui/Fonts.h"
#include "ui/Rect.h"
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
    cache_.clear();
    iconCache_.clear();
  }

private:
  using CachedText = render::TextTextureCache::Entry;

  static render::TextTextureCache::Style cacheStyle(const ui::TextStyle& style) {
    return render::TextTextureCache::Style {
      false, style.family == ui::FontFamily::Mono, style.strong, style.italic, style.size,
    };
  }

  const CachedText* iconTexture(const std::string& text, SDL_Color color) {
    if(const auto* hit = iconCache_.find(text, color, render::TextTextureCache::Style {})) return hit;
    SDL_Surface* surface = fonts_.renderIcon(text, color);
    if(!surface) return nullptr;
    SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
    CachedText entry {created, surface->w, surface->h};
    SDL_DestroySurface(surface);
    if(!created) return nullptr;
    return iconCache_.insert(text, color, render::TextTextureCache::Style {}, entry);
  }

  const CachedText* texture(const std::string& text, SDL_Color color, const ui::TextStyle& style) {
    const auto key = cacheStyle(style);
    if(const auto* hit = cache_.find(text, color, key)) return hit;
    perf::addCounter(perf::CounterId::RenderTextRasterizations);
    SDL_Surface* surface = fonts_.render(text, style, color);
    if(!surface) return nullptr;
    SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
    CachedText entry {created, surface->w, surface->h};
    SDL_DestroySurface(surface);
    if(!created) return nullptr;
    return cache_.insert(text, color, key, entry);
  }


  SDL_Renderer* renderer_ = nullptr;
  ui::FontStore fonts_;
  // Sized for a few full screens of text so the working set stays resident
  // while scrolling; icons get their own small cache so a glyph and a text run
  // that happen to share a string cannot collide.
  render::TextTextureCache cache_ {4096};
  render::TextTextureCache iconCache_ {256};
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
