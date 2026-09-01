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
