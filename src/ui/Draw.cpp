#include "ui/Draw.h"

#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {

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

void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor = theme().surface, SDL_Color borderColor = theme().hairline) {
  fill(renderer, rect, fillColor);
  stroke(renderer, rect, borderColor);
  hLine(renderer, rect.x + 1, rect.x + rect.w - 2, rect.y + 1, theme().surfaceSheen);
}

void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot) {
  // A rounded fill and nothing else.
  //
  // It used to be a fill plus a strip of accent down the left edge, and before
  // that an outline as well. The strip was there to say "this one" louder than
  // the fill could, on a palette where the selected fill was two shades off the
  // panel behind it. On this one it is not: the fill carries the whole message,
  // and a column of accent bars down a tree reads as a series of tabs.
  if(selected) fillRounded(renderer, row, theme().selectedBg, kRadiusSmall);
  else if(hot) fillRounded(renderer, row, theme().hoverBg, kRadiusSmall);
}

void drawFocusEdge(SDL_Renderer* renderer, Rect pane, bool focused) {
  if(!focused || pane.w <= 0.0f) return;
  fill(renderer, {pane.x, pane.y, kFocusEdgeWidth, pane.h}, theme().accentDim);
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
  drawSurface(renderer, rect, theme().surface, theme().hairline);
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

void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return;
  Rect track {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, std::max(24.0f, viewport.h - 18.0f)};
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  Rect thumb {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
  fill(renderer, track, theme().scrollTrack);
  fill(renderer, thumb, theme().scrollThumb);
  stroke(renderer, thumb, theme().scrollThumbBorder);
}

Rect scrollbarTrack(Rect viewport) {
  const float trackH = std::max(24.0f, viewport.h - 18.0f);
  return {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, trackH};
}

Rect scrollbarThumb(Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return {};
  const auto track = scrollbarTrack(viewport);
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  return {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
}

Rect scrollbarHitRect(Rect thumb) {
  return {thumb.x - 7.0f, thumb.y - 2.0f, thumb.w + 14.0f, thumb.h + 4.0f};
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
  drawRoundedSurface(renderer, card, theme().surfaceElevated, theme().hairline, kRadiusSmall);
  text.draw(tooltip.text, card.x + kTooltipPadX, card.y + kTooltipPadY, theme().text, style);
}

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style) {
  if(maxWidth <= 0) return "";
  if(text.width(value, style) <= maxWidth) return value;
  while(!value.empty() && text.width(value + "...", style) > maxWidth) value.pop_back();
  return value.empty() ? "..." : value + "...";
}

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading, bool mono) {
  return ellipsizeToWidth(text, std::move(value), maxWidth, TextRenderer::styleFor(heading, mono, false, false));
}


void drawSectionLabel(TextRenderer& text, std::string_view label, float x, float y) {
  text.draw(label, x, y, theme().dim);
}

void drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail, Rect rect,
                      std::string_view keys) {
  const TextStyle titleStyle {FontFamily::Sans, true, false, type().ui};
  const TextStyle bodyStyle {FontFamily::Sans, false, false, type().small};
  const TextStyle keyStyle {FontFamily::Sans, false, false, type().tiny};
  const int room = static_cast<int>(std::max(60.0f, rect.w - 36.0f));
  float y = rect.y + 14.0f;
  text.draw(ellipsizeToWidth(text, std::string(title), room, titleStyle), rect.x + 18.0f, y, theme().text, titleStyle);
  y += static_cast<float>(text.lineHeight(titleStyle)) + 6.0f;
  text.draw(ellipsizeToWidth(text, std::string(detail), room, bodyStyle), rect.x + 18.0f, y, theme().muted, bodyStyle);
  if(keys.empty()) return;
  y += static_cast<float>(text.lineHeight(bodyStyle)) + 8.0f;
  text.draw(ellipsizeToWidth(text, std::string(keys), room, keyStyle), rect.x + 18.0f, y, theme().dim, keyStyle);
}

}
