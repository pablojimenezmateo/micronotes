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

void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor = theme().surface, SDL_Color borderColor = theme().hairline) {
  fill(renderer, rect, fillColor);
  stroke(renderer, rect, borderColor);
  hLine(renderer, rect.x + 1, rect.x + rect.w - 2, rect.y + 1, theme().surfaceSheen);
}

void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot) {
  if(selected) {
    fill(renderer, row, theme().selectedBg);
    fill(renderer, {row.x, row.y, kSelectionStripWidth, row.h}, theme().accent);
  } else if(hot) {
    fill(renderer, row, theme().hoverBg);
  }
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
  fill(renderer, card, theme().surfaceElevated);
  stroke(renderer, card, theme().hairline);
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
