#include "ui/Painter.h"

#include "ui/Metrics.h"

namespace micronotes::ui {

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

void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor,
                 SDL_Color borderColor) {
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

void drawSurface(SDL_Renderer* renderer, Rect rect) {
  drawSurface(renderer, rect, theme().surfaceRaised, theme().border);
}

}
