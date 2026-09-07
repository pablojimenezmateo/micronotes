#include "CoreAliases.h"
#include "ui/Draw.h"

#include "core/editor/SoftWrap.h"
#include "ui/ColorMath.h"
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

void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor = theme().surfaceRaised,
                 SDL_Color borderColor = theme().border) {
  fill(renderer, rect, fillColor);
  // No sheen. A lit 1px top edge is what made a panel read as raised out of the
  // page; these panels are meant to read as cut into it, and the sheen was the
  // last thing left of the shell that had radii.
  if(borderColor.r == fillColor.r && borderColor.g == fillColor.g &&
     borderColor.b == fillColor.b && borderColor.a == fillColor.a) {
    return;
  }
  stroke(renderer, rect, borderColor);
}

void drawRow(SDL_Renderer* renderer, Rect row, SDL_Color base, bool emphasized, bool accentStrip) {
  fill(renderer, row, emphasized ? theme().rowHighlight : base);
  if(!emphasized || !accentStrip) return;
  fill(renderer, {row.x, row.y, kRowAccentWidth, row.h}, theme().accent);
}

void drawRow(SDL_Renderer* renderer, Rect row, bool selected, bool hot) {
  if(!selected && !hot) return;
  drawRow(renderer, row, theme().surfaceBackground, true, selected);
}

void drawFocusRing(SDL_Renderer* renderer, Rect pane, bool focused) {
  if(!focused || pane.w <= 0.0f || pane.h <= 0.0f) return;
  stroke(renderer, pane, theme().accent);
}

void drawChevron(SDL_Renderer* renderer, float x, float centerY, bool open, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(x);
  const float cy = std::round(centerY);
  if(open) {
    // Pointing down: two strokes meeting below their ends.
    SDL_RenderLine(renderer, cx, cy - 2.0f, cx + 4.0f, cy + 2.0f);
    SDL_RenderLine(renderer, cx + 8.0f, cy - 2.0f, cx + 4.0f, cy + 2.0f);
    return;
  }
  SDL_RenderLine(renderer, cx + 2.0f, cy - 4.0f, cx + 6.0f, cy);
  SDL_RenderLine(renderer, cx + 2.0f, cy + 4.0f, cx + 6.0f, cy);
}

void drawCloseGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 3.0f, cx + 3.0f, cy + 3.0f);
  SDL_RenderLine(renderer, cx + 3.0f, cy - 3.0f, cx - 3.0f, cy + 3.0f);
}

void drawCheckGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  // Two strokes, the short arm down-right and the long one up-right. Doubled a
  // pixel apart so the tick has some weight against a row's ground; a
  // single-pixel tick disappears next to the label beside it.
  for(float d = 0.0f; d <= 1.0f; d += 1.0f) {
    SDL_RenderLine(renderer, cx - 4.0f, cy + d, cx - 1.0f, cy + 3.0f + d);
    SDL_RenderLine(renderer, cx - 1.0f, cy + 3.0f + d, cx + 4.0f, cy - 3.0f + d);
  }
}

void drawArrowGlyph(SDL_Renderer* renderer, Rect box, bool pointRight, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const float arm = std::max(3.0f, box.h * 0.22f);
  const float dx = pointRight ? arm * 0.5f : -arm * 0.5f;
  SDL_RenderLine(renderer, cx - dx, cy - arm, cx + dx, cy);
  SDL_RenderLine(renderer, cx + dx, cy, cx - dx, cy + arm);
}

void drawSearchGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  // A ring and a handle. The ring is a rounded outline rather than a circle: at
  // twelve pixels the difference is invisible and the outline is exact.
  const float ring = std::max(6.0f, box.w - 4.0f);
  stroke(renderer, {box.x, box.y, ring, ring}, color);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, box.x + ring - 1.0f, box.y + ring - 1.0f, box.x + box.w - 1.0f,
                 box.y + box.h - 1.0f);
}

void drawStarGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const float r = std::max(4.0f, std::min(box.w, box.h) / 2.0f - 1.0f);
  // Five points, alternating outer and inner radius. Built as a polygon and
  // then either scan-filled or stroked, so the two states are the same shape --
  // a filled star and an outline of a different star would read as two marks.
  constexpr int kPoints = 10;
  SDL_FPoint hull[kPoints + 1];
  for(int i = 0; i < kPoints; ++i) {
    const float radius = (i % 2 == 0) ? r : r * 0.42f;
    // Starting at -90 degrees, so a point sits at the top where the eye looks.
    const float angle = -1.5707963f + static_cast<float>(i) * 3.14159265f / 5.0f;
    hull[i] = SDL_FPoint {cx + std::cos(angle) * radius, cy + std::sin(angle) * radius};
  }
  hull[kPoints] = hull[0];

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  if(!filled) {
    SDL_RenderLines(renderer, hull, kPoints + 1);
    return;
  }
  // Scanline fill: for each row, the span between the leftmost and rightmost
  // crossing of the outline. A star is not convex, so this over-fills the two
  // notches either side of the bottom points by a pixel or so -- at twelve
  // pixels that is what a filled star looks like anyway, and it costs one
  // submit rather than a triangulation.
  SDL_FRect spans[64];
  int count = 0;
  const int top = static_cast<int>(std::floor(cy - r));
  const int bottom = static_cast<int>(std::ceil(cy + r));
  for(int y = top; y <= bottom && count < 64; ++y) {
    const float row = static_cast<float>(y) + 0.5f;
    float left = 0.0f;
    float right = 0.0f;
    bool any = false;
    for(int i = 0; i < kPoints; ++i) {
      const SDL_FPoint a = hull[i];
      const SDL_FPoint b = hull[i + 1];
      if((row < a.y && row < b.y) || (row >= a.y && row >= b.y)) continue;
      const float t = (row - a.y) / (b.y - a.y);
      const float x = a.x + (b.x - a.x) * t;
      if(!any) {
        left = x;
        right = x;
        any = true;
        continue;
      }
      left = std::min(left, x);
      right = std::max(right, x);
    }
    if(!any || right - left < 0.5f) continue;
    spans[count++] = SDL_FRect {std::round(left), static_cast<float>(y),
                                std::round(right - left), 1.0f};
  }
  if(count > 0) SDL_RenderFillRects(renderer, spans, count);
}

void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized,
                     SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const auto line = [&](float x1, float y1, float x2, float y2) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, x1, y1, x2, y2);
  };
  if(which == 0) {
    hLine(renderer, cx - 4.0f, cx + 4.0f, cy, color);
    return;
  }
  if(which == 1) {
    if(maximized) {
      // Two offset outlines: the restored window in front of the space it
      // currently fills.
      stroke(renderer, {cx - 4.0f, cy - 1.0f, 7.0f, 7.0f}, color);
      hLine(renderer, cx - 1.0f, cx + 3.0f, cy - 4.0f, color);
      line(cx + 3.0f, cy - 4.0f, cx + 3.0f, cy + 1.0f);
    } else {
      stroke(renderer, {cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, color);
    }
    return;
  }
  line(cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f);
  line(cx - 4.0f, cy + 4.0f, cx + 4.0f, cy - 4.0f);
}

void drawTextFieldFrame(SDL_Renderer* renderer, Rect box, bool active) {
  fill(renderer, box, theme().surfaceBackground);
  stroke(renderer, box, active ? theme().accent : theme().border);
}

void drawSurface(SDL_Renderer* renderer, Rect rect) {
  drawSurface(renderer, rect, theme().surfaceRaised, theme().border);
}

std::optional<ScrollbarGeometry> scrollbarGeometry(Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0 || viewport.w <= 0.0f || viewport.h <= 0.0f) return std::nullopt;
  const float trackH = viewport.h - kScrollbarInset * 2.0f;
  if(trackH <= kScrollbarMinThumbLength) return std::nullopt;

  ScrollbarGeometry geometry;
  geometry.track = {viewport.x + viewport.w - kScrollbarThickness - kScrollbarInset,
                    viewport.y + kScrollbarInset, kScrollbarThickness, trackH};
  // What share of the whole the viewport shows. `maxScroll` is the pixels of
  // content past the bottom, so the whole is the viewport plus that.
  const float visible = viewport.h / (viewport.h + static_cast<float>(maxScroll));
  const float thumbH = std::clamp(trackH * visible, kScrollbarMinThumbLength, trackH);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  // The thumb fills the track's width. A narrower handle on a wider rail reads
  // as decoration until the pointer is already on it.
  geometry.thumb = {geometry.track.x, std::round(geometry.track.y + (trackH - thumbH) * t),
                    geometry.track.w, std::round(thumbH)};
  return geometry;
}

void drawScrollbar(SDL_Renderer* renderer, const ScrollbarGeometry& geometry, bool active) {
  fill(renderer, geometry.track, theme().surfaceRaised);
  // A resting thumb has to read as grabbable at a glance, so it is the muted
  // ink pulled most of the way toward the track rather than a shade of it; a
  // live drag takes the accent, so the grab gives a strong, distinct response.
  fill(renderer, geometry.thumb,
       active ? theme().accent : blend(theme().textMuted, theme().surfaceRaised, 0.6f));
}

void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll,
                           bool active) {
  if(const auto geometry = scrollbarGeometry(viewport, scroll, maxScroll)) {
    drawScrollbar(renderer, *geometry, active);
  }
}

Rect scrollbarHitRect(Rect thumb) {
  if(empty(thumb)) return thumb;
  return {thumb.x - kScrollbarHitInflate, thumb.y - kScrollbarHitInflate,
          thumb.w + kScrollbarHitInflate * 2.0f, thumb.h + kScrollbarHitInflate * 2.0f};
}

int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll) {
  const auto geometry = scrollbarGeometry(viewport, 0, maxScroll);
  if(!geometry) return 0;
  const float range = std::max(1.0f, geometry->track.h - geometry->thumb.h);
  const float t = std::clamp((y - dragOffsetY - geometry->track.y) / range, 0.0f, 1.0f);
  return static_cast<int>(std::round(t * static_cast<float>(maxScroll)));
}

void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds) {
  if(!tooltip.showing()) return;
  const TextStyle style {FontFamily::Sans, false, false, type().small};
  const float width = static_cast<float>(text.width(tooltip.text, style)) + kTooltipPadX * 2.0f;
  const float height = static_cast<float>(text.lineHeight(style)) + kTooltipPadY * 2.0f;
  const Rect card = placeTooltip(tooltip.anchor, width, height, bounds);
  drawSurface(renderer, card, theme().overlayBackground, theme().border);
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


Rect drawTitledCard(SDL_Renderer* renderer, Rect card, float headerHeight) {
  drawSurface(renderer, card, theme().overlayBackground, theme().border);
  const Rect header {card.x, card.y, card.w, std::max(0.0f, headerHeight)};
  if(header.h <= 0.0f) return header;
  // The chrome ground, so the header reads as the same kind of surface as the
  // menu bar and the tab strip rather than as the first row of whatever is
  // under it.
  fill(renderer, header, theme().chromeBackground);
  fill(renderer, {header.x, header.y + header.h - kDividerThickness, header.w, kDividerThickness},
       theme().border);
  return header;
}

void drawSectionBand(SDL_Renderer* renderer, TextRenderer& text, Rect band, Rect chevron,
                     std::string_view label, std::string_view trailing, bool collapsed,
                     bool hovered) {
  const bool collapsible = chevron.w > 0.0f;
  // A step up out of the panel, which is what makes it a band. Hover lifts it
  // one further, but only when there is something to shut: a caption that
  // brightens under the pointer is a caption promising a click it will not
  // honour.
  fill(renderer, band, hovered && collapsible ? theme().rowHighlight : theme().surfaceRaised);
  // The rule goes along the *top*, so it separates this band from the group
  // above rather than from the rows it heads -- those belong to it.
  fill(renderer, {band.x, band.y, band.w, kDividerThickness}, theme().border);

  // The chrome size, not a step down from it. At 0.85 of it the label came out
  // 11px, and 11px uppercase in a mono face with `textSecondary`'s ink has
  // stems the rasterizer spreads over two columns at partial coverage -- so the
  // headings read as blurred next to the 13px rows they head, which is the one
  // thing a heading must not do. microide's section headers are set at its
  // single chrome size for the same reason; what distinguishes a band from a
  // row is its ground and its outdent, never a smaller type.
  const TextStyle style = chromeStyle();
  float right = band.x + band.w - kSidebarInset;
  if(!trailing.empty()) {
    const float width = static_cast<float>(text.width(trailing, style));
    text.draw(trailing, right - width, textTop(band, text, style), theme().textMuted, style);
    right -= width + kSpace2;
  }
  // Outdented an indent step ahead of the rows under it. See `kSectionLabelX`.
  const float labelX = band.x + kSectionLabelX;
  text.draw(ellipsizeToWidth(text, std::string(label), static_cast<int>(std::max(0.0f, right - labelX)), style),
            labelX, textTop(band, text, style),
            hovered && collapsible ? theme().textPrimary : theme().textSecondary, style);
  if(collapsible) {
    drawChevron(renderer, chevron.x, chevron.y + chevron.h / 2.0f, !collapsed,
                hovered ? theme().textPrimary : theme().textMuted);
  }
}

void drawTagDot(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  // A solid disc, drawn as spans rather than as a texture: at seven pixels a
  // circle is a handful of rows and the arithmetic is cheaper than a cache
  // lookup.
  //
  // Solid, and not an outline for one state and a fill for another. That was
  // tried, to mark the tag being filtered by, and at this size the ring's hole
  // was over half the dot -- so the mark read as a small "0" rather than as a
  // dot, in every row, to distinguish a state the row's own accent strip
  // already says. Two pixels is not enough room for two states.
  const float radius = std::min(box.w, box.h) / 2.0f;
  const float cx = box.x + box.w / 2.0f;
  const float cy = box.y + box.h / 2.0f;
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  for(float dy = -radius; dy <= radius; dy += 1.0f) {
    const float half = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
    if(half <= 0.0f) continue;
    SDL_RenderLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
  }
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

void drawButton(SDL_Renderer* renderer, TextRenderer& text, Rect box, std::string_view label,
                bool enabled, bool hovered, ButtonTone tone) {
  SDL_Color fillColor = theme().surfaceRaised;
  SDL_Color borderColor = theme().border;
  SDL_Color ink = theme().textPrimary;
  if(tone == ButtonTone::Accent) {
    fillColor = theme().accent;
    borderColor = theme().accent;
    ink = theme().onAccent;
  } else if(tone == ButtonTone::Destructive) {
    fillColor = theme().warn;
    borderColor = theme().warn;
    ink = theme().onAccent;
  }
  if(!enabled) {
    fillColor = theme().surfaceBackground;
    borderColor = theme().border;
    ink = theme().textDisabled;
  } else if(hovered) {
    // Toward the ink rather than toward a second fill: one blend serves all
    // three tones, where a hover colour per tone is three more constants that
    // have to be kept in step with the three they hover over.
    fillColor = blend(fillColor, ink, 0.14f);
  }
  fill(renderer, box, fillColor);
  stroke(renderer, box, borderColor);

  const TextStyle style = chromeStyle();
  const float width = static_cast<float>(text.width(label, style));
  text.draw(label, std::round(box.x + (box.w - width) / 2.0f), textTop(box, text, style), ink, style);
}

void drawMenuRow(SDL_Renderer* renderer, TextRenderer& text, Rect row, std::string_view label,
                 std::string_view accelerator, bool enabled, bool hovered, bool checked,
                 bool destructive) {
  if(hovered && enabled) fill(renderer, row, theme().rowHighlight);
  const TextStyle style = chromeStyle();
  const SDL_Color ink = !enabled ? theme().textDisabled
                       : destructive ? theme().warn
                       : hovered ? theme().textPrimary
                                 : theme().textSecondary;
  if(checked) {
    drawCheckGlyph(renderer, {row.x + kSpace2, row.y, 12.0f, row.h},
                   enabled ? theme().accent : theme().textDisabled);
  }
  const float baseline = textTop(row, text, style);
  const float acceleratorWidth =
    accelerator.empty() ? 0.0f : static_cast<float>(text.width(accelerator, style));
  const float labelRoom = row.w - kMenuPopupLabelInset - kMenuPopupAcceleratorInset -
                          acceleratorWidth - kSpace2;
  text.draw(ellipsizeToWidth(text, std::string(label), static_cast<int>(labelRoom), style),
            row.x + kMenuPopupLabelInset, baseline, ink, style);
  if(accelerator.empty()) return;
  // The accelerator is never ellipsized: a chord with its tail cut off is worse
  // than no chord at all, and the popup's width was measured to hold it.
  text.draw(accelerator, row.x + row.w - acceleratorWidth - kMenuPopupAcceleratorInset, baseline,
            enabled ? theme().textMuted : theme().textDisabled, style);
}

StripTabColors stripTabColors() {
  return {
    theme().chromeActive,
    // A step *up* from the strip's own ground, not level with it. An inactive
    // tab filled with the strip's colour is not a tab: with two of them side by
    // side the only thing saying where one ended was the 1px rule between them,
    // so a strip of three notes read as one wide empty band with some words in
    // it. The active tab is then a further step up, plus its accent lid.
    theme().surfaceRaised,
    theme().rowHighlight,
    theme().chromeActiveText,
    theme().chromeTextSecondary,
  };
}

void drawStripTab(SDL_Renderer* renderer, TextRenderer& text, Rect rect, std::string_view label,
                  bool active, bool hovered, float closeReserve, const StripTabColors& colors) {
  const SDL_Color background = active ? colors.activeFill
                             : hovered ? colors.hoverFill
                                       : colors.inactiveFill;
  fill(renderer, rect, background);
  // The lid, not an outline: it says which tab the page below belongs to, and
  // an outline would say "this tab is selected" about a strip where exactly one
  // tab is always selected.
  if(active) fill(renderer, {rect.x, rect.y, rect.w, kRowAccentWidth}, theme().accent);
  // A rule between one tab and the next, so two inactive neighbours do not read
  // as one wide tab. Skipped on the active one, which its own fill separates.
  if(!active) {
    fill(renderer, {rect.x + rect.w - kDividerThickness, rect.y + kSpace1, kDividerThickness,
                    rect.h - kSpace1 * 2.0f}, theme().border);
  }

  const TextStyle style = chromeStyle();
  const float left = rect.x + kSidebarInset;
  const int room = static_cast<int>(rect.x + rect.w - closeReserve - left);
  text.draw(ellipsizeToWidth(text, std::string(label), room, style), left,
            textTop(rect, text, style), active ? colors.activeText : colors.inactiveText, style);
}

void drawStripOverflowButton(SDL_Renderer* renderer, TextRenderer& text, Rect box,
                             bool pointRight, std::size_t hidden, bool hovered) {
  if(hidden == 0 || empty(box)) return;
  const SDL_Color background = hovered ? theme().rowHighlight : theme().chromeBackground;
  const SDL_Color ink = hovered ? theme().textPrimary : theme().chromeTextSecondary;
  fill(renderer, box, background);
  stroke(renderer, box, theme().border);
  drawArrowGlyph(renderer, {box.x, box.y, 16.0f, box.h}, pointRight, ink);

  const TextStyle style = chromeSmallStyle();
  const std::string count = std::to_string(hidden);
  const float width = static_cast<float>(text.width(count, style));
  if(width + 18.0f > box.w) return;
  text.draw(count, box.x + box.w - width - kSpace1, textTop(box, text, style), ink, style);
}

}
