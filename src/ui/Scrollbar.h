#pragma once

#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <optional>

// Where a scrolling surface's scrollbar goes, what it looks like, and what
// grabbing it means.
//
// Four askers -- the paint, the hit test, the cursor shape and the drag that
// moves it -- and one answer, which is the whole point: `PageView` used to
// carry a private copy of these numbers, paint the live page's bar from it and
// be hit-tested against this one, so the two agreeing was a coincidence rather
// than a fact (TD-20).
namespace micronotes::ui {

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

}
