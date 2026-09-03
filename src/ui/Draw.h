#pragma once

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/render/TextTextureCache.h"

#include "ui/Fonts.h"
#include "ui/TextMeasureCache.h"
#include "ui/Rect.h"
#include "ui/Tooltip.h"
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

// One-time renderer setup the palette depends on.
//
// Seven colours in `Theme` carry an alpha on purpose: the text selection, the
// find-match fill and its border, the scrollbar track and thumb border, and the
// raised-surface sheen. SDL's default draw blend mode is SDL_BLENDMODE_NONE,
// which writes the alpha byte and then ignores it, so every one of those painted
// opaque -- a scrollbar track set to alpha zero, meaning "do not draw me", drew
// a solid bar down the edge of every list instead.
//
// Worse, it was not consistent within a run. The overlay stack turned blending
// on before dimming the window behind a palette and never turned it back off,
// so a session that had opened the command palette once painted the rest of the
// shell differently from one that had not. Blending belongs to the renderer for
// its whole life, and this is where that is said.
void configureRenderer(SDL_Renderer* renderer);

void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void hLine(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color);

// A filled rectangle with rounded corners, assembled from horizontal spans: a
// centre block, and one span per scanline of the two corner bands. At the radii
// the shell actually uses that is a couple of dozen spans submitted in a single
// SDL_RenderFillRects, which is cheaper than the texture a general rounded-rect
// routine would want to cache and invalidate.
//
// A radius of zero, or one too large for the rect to hold, degrades to the
// square fill rather than to a shape nobody asked for -- callers pass a token,
// and a token has no idea how small the rect it lands in has been squeezed.
void fillRounded(SDL_Renderer* renderer, Rect rect, SDL_Color color, float radius);
void strokeRounded(SDL_Renderer* renderer, Rect rect, SDL_Color color, float radius);
void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor);
void drawSurface(SDL_Renderer* renderer, Rect rect);
// The rounded counterpart of drawSurface: fill, border, and no sheen. The sheen
// is a Notion device -- a 1px lit top edge on a raised panel -- and it reads as
// a seam once the corners are round.
void drawRoundedSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor,
                        float radius);
// A row that is selected, pointed at, or neither.
//
// Selection is a fill and a strip of accent down the left edge -- the shape of
// a marker in a margin. It used to also outline the row, which made a list of
// rows read as a list of boxes and made "selected" and "focused" look the same
// as each other.
void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot);

// Which surface has the keyboard. Drawn as a strip down the edge nearest the
// rest of the window rather than as an outline round the whole pane: an outline
// competes with every border already on screen, and at this size reads as a
// selected control rather than as "typing goes here".
void drawFocusEdge(SDL_Renderer* renderer, Rect pane, bool focused);
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

  // Draws an emoji scaled to fit inside `box` and centred there. A colour emoji
  // font is one fixed bitmap strike that SDL_ttf cannot resize, so the glyph
  // arrives at 128 pixels whatever was asked for; scaling the rendered texture
  // is the only place its size can be honoured. Returns false when no emoji
  // face is installed, so the caller can draw its own mark instead of tofu.
  bool drawIcon(std::string_view value, Rect box, SDL_Color color) {
    if(value.empty() || !fonts_.ready() || box.w <= 0.0f || box.h <= 0.0f) return false;
    if(!fonts_.hasIconFont()) return false;
    const CachedText* cached = iconTexture(value, color);
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

  // The physical size an icon is cached at. Icons are drawn into a row-height
  // box -- 16 logical pixels everywhere the shell uses one -- and a colour
  // emoji face hands back one fixed bitmap strike, 136 pixels for Noto. Two
  // pixels of source per pixel of destination is as far as the renderer's own
  // bilinear filter can be trusted; beyond that it samples four texels out of
  // seventy and the glyph arrives as noise. So the surface is walked down to
  // this size first and the renderer is left the gentle last step.
  static constexpr int kIconCachePx = 32;

  // Halve until one more halving would overshoot, then land exactly on the
  // target. Repeated halving is a box filter over every source pixel, which is
  // what a single large downscale is missing; doing it on the surface costs one
  // pass per step, once, against every frame the texture is drawn.
  static SDL_Surface* downscale(SDL_Surface* surface, int target) {
    while(surface && surface->w > target * 2 && surface->h > target * 2) {
      SDL_Surface* half = SDL_ScaleSurface(surface, surface->w / 2, surface->h / 2, SDL_SCALEMODE_LINEAR);
      if(!half) return surface;
      SDL_DestroySurface(surface);
      surface = half;
    }
    if(!surface || (surface->w <= target && surface->h <= target)) return surface;
    const float fit = std::min(static_cast<float>(target) / static_cast<float>(surface->w),
                               static_cast<float>(target) / static_cast<float>(surface->h));
    SDL_Surface* fitted = SDL_ScaleSurface(surface, std::max(1, static_cast<int>(std::lround(surface->w * fit))),
                                           std::max(1, static_cast<int>(std::lround(surface->h * fit))),
                                           SDL_SCALEMODE_LINEAR);
    if(!fitted) return surface;
    SDL_DestroySurface(surface);
    return fitted;
  }

  const CachedText* iconTexture(std::string_view text, SDL_Color color) {
    if(const auto* hit = iconCache_.find(text, color, render::TextTextureCache::Style {})) return hit;
    SDL_Surface* surface = fonts_.renderIcon(text, color);
    if(!surface) return nullptr;
    // Cached at one size for every caller, because the display scale is in the
    // key of nothing here and clear() drops the cache when it changes.
    surface = downscale(surface, static_cast<int>(std::lround(kIconCachePx * fonts_.displayScale())));
    if(!surface) return nullptr;
    SDL_Texture* created = SDL_CreateTextureFromSurface(renderer_, surface);
    CachedText entry {created, surface->w, surface->h};
    SDL_DestroySurface(surface);
    if(!created) return nullptr;
    return iconCache_.insert(text, color, render::TextTextureCache::Style {}, entry);
  }

  const CachedText* texture(std::string_view text, SDL_Color color, const ui::TextStyle& style) {
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
  // Widths are asked for far more often than textures -- once per word of a
  // layout pass against once per drawn run -- and an entry is 24 bytes rather
  // than a texture, so this is sized an order of magnitude larger. Mutable
  // because measuring is a query: a caller asking how wide a string is has not
  // changed anything a caller can observe.
  mutable ui::TextMeasureCache measures_ {1 << 16};
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

// A section heading over a list of rows: FAVORITES, TAGS, RECENT.
void drawSectionLabel(TextRenderer& text, std::string_view label, float x, float y);

// An empty place says what it is, what to do about it, and which keys do that.
// The third line is what turns a dead end into an offer, so it is dimmer than
// the rest rather than left out.
void drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail, Rect rect,
                      std::string_view keys = {});

// A vertical scrollbar down the right of a viewport, drawn only when there is
// something to scroll. The geometry is exposed because the hit test and the
// drag both have to agree with what was painted.
void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll);
Rect scrollbarTrack(Rect viewport);
Rect scrollbarThumb(Rect viewport, int scroll, int maxScroll);
// The thumb, grown so it can be grabbed. One inflate governs the grab region
// and the region that changes the cursor, so they cannot drift apart.
Rect scrollbarHitRect(Rect thumb);
int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll);

// Draws a resolved tooltip. Called last in a frame, so nothing paints over it.
void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds);

// Shortens `value` with an ellipsis until it fits `maxWidth`.
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style);
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading = false, bool mono = false);

}
