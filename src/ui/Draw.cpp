#include "CoreAliases.h"
#include "ui/Draw.h"

#include "core/editor/SoftWrap.h"
#include "ui/Metrics.h"
#include "ui/TextUtil.h"

#include <algorithm>
#include <cmath>
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

void configureRenderer(SDL_Renderer* renderer) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderFillRect(renderer, &out);
}

void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderRect(renderer, &out);
}

void hLine(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, x1, y, x2, y);
}

namespace {

// The largest radius the span buffer below is sized for. Nothing in the shell
// asks for a corner this round; the cap is here so a bad number cannot walk off
// the end of the array rather than because a rounder corner was refused.
constexpr int kMaxCornerRadius = 24;

// How far in from the edge the fill starts on scanline `i` of a corner band.
// Measured from the middle of the scanline, so the curve sits where the eye
// expects rather than half a pixel high.
float cornerInset(float radius, int i) {
  const float dy = radius - static_cast<float>(i) - 0.5f;
  const float inner = radius * radius - dy * dy;
  return radius - std::sqrt(std::max(0.0f, inner));
}

// The radius a rect can actually hold. A token is chosen for how the shape
// should read, not for how small the rect will be when a panel is dragged
// narrow, so the clamp belongs here and not at every call site.
float usableRadius(Rect rect, float radius) {
  return std::clamp(radius, 0.0f, std::min({static_cast<float>(kMaxCornerRadius), rect.w / 2.0f, rect.h / 2.0f}));
}

}

void fillRounded(SDL_Renderer* renderer, Rect rect, SDL_Color color, float radius) {
  const float r = usableRadius(rect, radius);
  if(r < 1.0f || rect.w <= 0.0f || rect.h <= 0.0f) {
    fill(renderer, rect, color);
    return;
  }
  const int band = static_cast<int>(r);
  // Two spans per corner scanline plus the block between them.
  SDL_FRect spans[kMaxCornerRadius * 2 + 1];
  int count = 0;
  spans[count++] = SDL_FRect {rect.x, rect.y + r, rect.w, rect.h - 2.0f * r};
  for(int i = 0; i < band; ++i) {
    const float inset = cornerInset(r, i);
    const float width = rect.w - 2.0f * inset;
    if(width <= 0.0f) continue;
    spans[count++] = SDL_FRect {rect.x + inset, rect.y + static_cast<float>(i), width, 1.0f};
    spans[count++] = SDL_FRect {rect.x + inset, rect.y + rect.h - static_cast<float>(i) - 1.0f, width, 1.0f};
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRects(renderer, spans, count);
}

void strokeRounded(SDL_Renderer* renderer, Rect rect, SDL_Color color, float radius) {
  const float r = usableRadius(rect, radius);
  if(r < 1.0f || rect.w <= 0.0f || rect.h <= 0.0f) {
    stroke(renderer, rect, color);
    return;
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float right = rect.x + rect.w;
  const float bottom = rect.y + rect.h;
  // The four straight edges, each stopping where its corners begin.
  SDL_RenderLine(renderer, rect.x + r, rect.y, right - r - 1.0f, rect.y);
  SDL_RenderLine(renderer, rect.x + r, bottom - 1.0f, right - r - 1.0f, bottom - 1.0f);
  SDL_RenderLine(renderer, rect.x, rect.y + r, rect.x, bottom - r - 1.0f);
  SDL_RenderLine(renderer, right - 1.0f, rect.y + r, right - 1.0f, bottom - r - 1.0f);
  // The corners, one point per scanline. Four points share each scanline, which
  // is why they are gathered rather than drawn one call at a time.
  SDL_FPoint points[kMaxCornerRadius * 4];
  int count = 0;
  const int band = static_cast<int>(r);
  for(int i = 0; i < band; ++i) {
    const float inset = cornerInset(r, i);
    const float y = rect.y + static_cast<float>(i);
    const float flipped = bottom - static_cast<float>(i) - 1.0f;
    points[count++] = SDL_FPoint {rect.x + inset, y};
    points[count++] = SDL_FPoint {right - inset - 1.0f, y};
    points[count++] = SDL_FPoint {rect.x + inset, flipped};
    points[count++] = SDL_FPoint {right - inset - 1.0f, flipped};
  }
  SDL_RenderPoints(renderer, points, count);
}

void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor = theme().surfaceRaised, SDL_Color borderColor = theme().border) {
  fill(renderer, rect, fillColor);
  stroke(renderer, rect, borderColor);
  hLine(renderer, rect.x + 1, rect.x + rect.w - 2, rect.y + 1, theme().border);
}

void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot) {
  // A rounded fill and nothing else.
  //
  // It used to be a fill plus a strip of accent down the left edge, and before
  // that an outline as well. The strip was there to say "this one" louder than
  // the fill could, on a palette where the selected fill was two shades off the
  // panel behind it. On this one it is not: the fill carries the whole message,
  // and a column of accent bars down a tree reads as a series of tabs.
  if(selected) fillRounded(renderer, row, theme().rowHighlight, kRadiusSmall);
  else if(hot) fillRounded(renderer, row, theme().rowHighlight, kRadiusSmall);
}

void drawFocusEdge(SDL_Renderer* renderer, Rect pane, bool focused) {
  if(!focused || pane.w <= 0.0f) return;
  fill(renderer, {pane.x, pane.y, kFocusEdgeWidth, pane.h}, theme().accent);
}

void drawDisclosure(SDL_Renderer* renderer, Rect box, bool open, SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  for(int i = 0; i < 5; ++i) {
    const float span = 4.0f - static_cast<float>(i);
    if(open) SDL_RenderLine(renderer, cx - span, cy - 2.0f + static_cast<float>(i), cx + span, cy - 2.0f + static_cast<float>(i));
    else SDL_RenderLine(renderer, cx - 2.0f + static_cast<float>(i), cy - span, cx - 2.0f + static_cast<float>(i), cy + span);
  }
}

void drawSurface(SDL_Renderer* renderer, Rect rect) {
  drawSurface(renderer, rect, theme().surfaceRaised, theme().border);
}

void drawRoundedSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor,
                        float radius) {
  fillRounded(renderer, rect, fillColor, radius);
  // A border the same colour as the fill is how a caller asks for a surface
  // with no edge at all; stroking it anyway would only cost a draw call.
  if(borderColor.r == fillColor.r && borderColor.g == fillColor.g && borderColor.b == fillColor.b &&
     borderColor.a == fillColor.a) {
    return;
  }
  strokeRounded(renderer, rect, borderColor, radius);
}

Rect scrollbarTrack(Rect viewport) {
  return {viewport.x + viewport.w - kScrollbarInsetX, viewport.y + kScrollbarInsetY,
          kScrollbarTrackWidth,
          std::max(kScrollbarMinTrack, viewport.h - kScrollbarInsetY * 2.0f)};
}

Rect scrollbarThumb(Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return {};
  const auto track = scrollbarTrack(viewport);
  const float visibleRatio =
    std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), kScrollbarMinThumbRatio, 1.0f);
  const float thumbH = std::max(kScrollbarMinThumb, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  // Centred on the track, which is what makes the thumb read as a handle on it
  // rather than as a wider fill of it.
  return {track.x - (kScrollbarThumbWidth - kScrollbarTrackWidth) / 2.0f,
          track.y + (track.h - thumbH) * t, kScrollbarThumbWidth, thumbH};
}

// Composed from the two above rather than repeating their arithmetic, so what
// is painted and what is hit-tested cannot drift. They did: `PageView` carried
// a private fourth copy of these numbers, painted the live page's scrollbar
// from it, and was hit-tested against these -- identical only by luck.
void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return;
  fill(renderer, scrollbarTrack(viewport), theme().surfaceRaised);
  const Rect thumb = scrollbarThumb(viewport, scroll, maxScroll);
  fill(renderer, thumb, theme().textMuted);
  stroke(renderer, thumb, theme().border);
}

Rect scrollbarHitRect(Rect thumb) {
  return {thumb.x - kScrollbarInsetX, thumb.y - kScrollbarHitInflate / 2.0f,
          thumb.w + kScrollbarInsetX * 2.0f, thumb.h + kScrollbarHitInflate};
}

int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll) {
  const auto track = scrollbarTrack(viewport);
  const auto thumb = scrollbarThumb(viewport, 0, maxScroll);
  const float range = std::max(1.0f, track.h - thumb.h);
  const float t = std::clamp((y - dragOffsetY - track.y) / range, 0.0f, 1.0f);
  return static_cast<int>(std::round(t * static_cast<float>(maxScroll)));
}

void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds) {
  if(!tooltip.showing()) return;
  const TextStyle style {FontFamily::Sans, false, false, type().small};
  const float width = static_cast<float>(text.width(tooltip.text, style)) + kTooltipPadX * 2.0f;
  const float height = static_cast<float>(text.lineHeight(style)) + kTooltipPadY * 2.0f;
  const Rect card = placeTooltip(tooltip.anchor, width, height, bounds);
  drawRoundedSurface(renderer, card, theme().overlayBackground, theme().border, kRadiusSmall);
  text.draw(tooltip.text, card.x + kTooltipPadX, card.y + kTooltipPadY, theme().textPrimary, style);
}

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style) {
  perf::addCounter(perf::CounterId::ShellEllipsizeCalls);
  return ellipsizeToFit(std::move(value), maxWidth, [&](std::string_view candidate) {
    perf::addCounter(perf::CounterId::ShellEllipsizeMeasures);
    return text.width(candidate, style);
  });
}

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading, bool mono) {
  return ellipsizeToWidth(text, std::move(value), maxWidth, TextRenderer::styleFor(heading, mono, false, false));
}


void drawSectionLabel(TextRenderer& text, std::string_view label, float x, float y) {
  text.draw(label, x, y, theme().textMuted);
}

float drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail,
                       float x, float y, float width, std::string_view keys) {
  const TextStyle titleStyle {FontFamily::Sans, true, false, type().ui};
  const TextStyle bodyStyle {FontFamily::Sans, false, false, type().small};
  const TextStyle keyStyle {FontFamily::Sans, false, false, type().tiny};
  const int room = static_cast<int>(std::max(60.0f, width - 36.0f));
  const float top = y;
  y += 14.0f;
  text.draw(ellipsizeToWidth(text, std::string(title), room, titleStyle), x + 18.0f, y, theme().textPrimary, titleStyle);
  y += static_cast<float>(text.lineHeight(titleStyle)) + 6.0f;

  // The detail wraps rather than being cut off at the column. Every one of
  // these messages is drawn in a panel -- the sidebar, the outline -- narrow
  // enough that a sentence does not fit on one line, and half a sentence with
  // an ellipsis after it says less than nothing: "Notes here are plain .md..."
  // was the whole of what a fresh library had to say for itself.
  //
  // Bounded, because the box is: past three lines the message is no longer an
  // empty state, and the last of them takes the ellipsis instead.
  static constexpr std::size_t kMaxLines = 3;
  const auto rows = editor::softWrap(detail, room, [&](std::string_view value) {
    return text.width(value, bodyStyle);
  });
  const float bodyStep = static_cast<float>(text.lineHeight(bodyStyle));
  for(std::size_t i = 0; i < rows.size() && i < kMaxLines; ++i) {
    const bool last = i + 1 == kMaxLines && rows.size() > kMaxLines;
    text.draw(last ? ellipsizeToWidth(text, rows[i].text + "...", room, bodyStyle) : rows[i].text,
              x + 18.0f, y, theme().textSecondary, bodyStyle);
    y += bodyStep;
  }

  if(!keys.empty()) {
    y += 8.0f;
    text.draw(ellipsizeToWidth(text, std::string(keys), room, keyStyle), x + 18.0f, y, theme().textMuted, keyStyle);
    y += static_cast<float>(text.lineHeight(keyStyle));
  }
  return y + 14.0f - top;
}

}
