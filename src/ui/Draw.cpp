#include "ui/Draw.h"

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
    fill(renderer, {row.x, row.y, 3, row.h}, theme().accent);
    stroke(renderer, row, theme().accentDim);
  } else if(hot) {
    fill(renderer, row, theme().hoverBg);
    stroke(renderer, row, theme().hairline);
  }
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

}
