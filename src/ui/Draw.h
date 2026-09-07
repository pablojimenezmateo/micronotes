#pragma once

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/render/TextTextureCache.h"

#include "ui/ClipGuard.h"
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
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

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

// Nothing here rounds a corner.
//
// The shell used to have three radii -- one for controls, one for blocks in the
// page, one for what floats over it -- which is three answers to a question a
// flat interface does not ask. What separates two surfaces now is that their
// fills differ and, where that is not enough, a single-weight 1px rule; and
// what makes a control a control is its ground, not its silhouette. See
// `Theme`'s contrast corrector, which is what makes that hold up.
void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor);
void drawSurface(SDL_Renderer* renderer, Rect rect);

// A row in a list: selected, pointed at, or neither.
//
// Hover and selection share a ground, and the 2px strip of accent down the
// leading edge is the whole of what says "this one" rather than "this is what
// I would click". Three shades for three states is three shades a reader cannot
// tell apart; a strip is a difference in kind.
void drawRow(SDL_Renderer* renderer, Rect row, SDL_Color base, bool emphasized,
             bool accentStrip = false);
// The same, against the panel ground, for the callers that have no other base.
void drawRow(SDL_Renderer* renderer, Rect row, bool selected, bool hot);

// Which surface has the keyboard, as a 1px outline round the pane. An outline
// rather than an edge strip: the strip is what a selected *row* wears, and one
// device cannot mean two things.
void drawFocusRing(SDL_Renderer* renderer, Rect pane, bool focused);

// A folder's disclosure mark: two short strokes meeting at a point, down when
// open and right when shut. Drawn rather than typeset -- the mono face has no
// glyph for one, and two lines stay exact at any scale where a glyph would be
// resampled into a smudge.
void drawChevron(SDL_Renderer* renderer, float x, float centerY, bool open, SDL_Color color);

// A close cross, and a tick. Both drawn for the same reason as the chevron.
void drawCloseGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);
void drawCheckGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);
// A single-headed arrow pointing left or right, for the tab strip's overflow
// buttons and anything else that scrolls a strip.
void drawArrowGlyph(SDL_Renderer* renderer, Rect box, bool pointRight, SDL_Color color);

// A magnifier, for the search field.
void drawSearchGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);

// The favourite mark: filled when the note is kept, an outline when it is an
// offer. Drawn rather than typeset because the chrome face is a mono
// programming face and has neither star -- the breadcrumb used to set them from
// the proportional face, and switching the chrome to mono left tofu where the
// star had been.
void drawStarGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color);
// Minimise, maximise (or restore), close: `which` is 0, 1, 2 in that order,
// which is the order they are laid out in.
void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized,
                     SDL_Color color);

// A text field's frame: the panel ground with a 1px rule round it, accent when
// it has the keyboard.
void drawTextFieldFrame(SDL_Renderer* renderer, Rect box, bool active);

// A button with its label centred. `tone` picks the fill: a neutral button
// takes the raised ground, a destructive one takes the warning colour.
enum class ButtonTone {
  Neutral,
  Accent,
  Destructive
};

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

  void draw(std::string_view value, float x, float y, SDL_Color color = theme().textPrimary, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) {
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
    const auto key = render::TextTextureCache::makeKey(text, color, render::TextTextureCache::Style {});
    if(const auto* hit = iconCache_.find(key)) return hit;
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
    return iconCache_.insert(key, entry);
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
//
// Takes an origin and a width, and **returns the height it used**, because that
// height is not something a caller can know: the detail wraps to the column and
// the key line is optional, so the message is between two and five lines tall
// depending on the text and on the reader's text size. Every caller used to
// pass a rect with a made-up height -- 100, 110, 120 -- which nothing checked
// and nothing read, so at the large text size a three-line empty state ran past
// the box it claimed to be in. Nothing was drawn under any of them, which is
// why nobody noticed; the first surface to put something there would have
// inherited the bug.
float drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail,
                       float x, float y, float width, std::string_view keys = {});

// A vertical scrollbar down the right of a viewport, drawn only when there is
// something to scroll. The geometry is exposed because the hit test and the
// drag both have to agree with what was painted.
// Where a scrolling surface's scrollbar goes, or nothing when the content fits.
//
// One function, asked by the paint, by the hit test, by the cursor shape and by
// the drag that moves it -- so what is drawn and what responds cannot drift.
// They did: `PageView` carried a private copy of these numbers, painted the
// live page's scrollbar from it, and was hit-tested against ui's, so the two
// agreeing was a coincidence rather than a fact (TD-20).
struct ScrollbarGeometry {
  Rect track;
  Rect thumb;

  friend bool operator==(const ScrollbarGeometry&, const ScrollbarGeometry&) = default;
};

std::optional<ScrollbarGeometry> scrollbarGeometry(Rect viewport, int scroll, int maxScroll);

// `active` is a live drag, which takes the accent: a thumb being dragged should
// answer visibly, and at rest it should read as grabbable without shouting.
void drawScrollbar(SDL_Renderer* renderer, const ScrollbarGeometry& geometry, bool active);
void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll,
                           bool active = false);
// The thumb, grown so it can be grabbed. One inflate governs the grab region
// and the region that changes the cursor, so they cannot drift apart.
Rect scrollbarHitRect(Rect thumb);
int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll);

// Draws a resolved tooltip. Called last in a frame, so nothing paints over it.
void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds);

// Shortens `value` with an ellipsis until it fits `maxWidth`.
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style);
std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading = false, bool mono = false);

// A button with its label centred, in the chrome face.
void drawButton(SDL_Renderer* renderer, TextRenderer& text, Rect box, std::string_view label,
                bool enabled, bool hovered, ButtonTone tone = ButtonTone::Neutral);

// One row of a menu: the tick slot, the label, and the accelerator right up
// against the trailing edge.
//
// Shared by the menu bar's popups, the context menus and the command palette,
// because all three are the same object. They used to be two: an `Overlay`
// list row and, in microide, a `DrawMenuRow` -- with different heights, so a
// context menu and the palette that can run the same command looked unrelated.
void drawMenuRow(SDL_Renderer* renderer, TextRenderer& text, Rect row, std::string_view label,
                 std::string_view accelerator, bool enabled, bool hovered, bool checked,
                 bool destructive = false);

// One tab in a strip: a flat fill, a 2px accent lid when it is the active one,
// the title, and the room its close cross needs kept clear.
//
// The lid is what makes the active tab read as continuous with the page under
// it. The tab used to be rounded at the top and square at the foot, drawn a
// corner taller than the strip so the clip took the bottom corners off -- a
// shape that only works while the strip's ground and the page's differ by
// exactly the right amount.
struct StripTabColors {
  SDL_Color activeFill;
  SDL_Color inactiveFill;
  SDL_Color hoverFill;
  SDL_Color activeText;
  SDL_Color inactiveText;
};

StripTabColors stripTabColors();

void drawStripTab(SDL_Renderer* renderer, TextRenderer& text, Rect rect, std::string_view label,
                  bool active, bool hovered, float closeReserve, const StripTabColors& colors);

// The chevron button at a strip's end, with the number of tabs hidden past it.
// Zero hidden draws nothing at all, so a strip that fits has no furniture.
void drawStripOverflowButton(SDL_Renderer* renderer, TextRenderer& text, Rect box,
                             bool pointRight, std::size_t hidden, bool hovered);

}
