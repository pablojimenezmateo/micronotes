#include "ui/Scrollbar.h"

#include "ui/Metrics.h"
#include "ui/Painter.h"
#include "ui/Theme.h"
#include "CoreAliases.h"
#include "core/render/ColorMath.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {

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
       active ? theme().accent : render::blend(theme().textMuted, theme().surfaceRaised, 0.6f));
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

}
