#include "TestSupport.h"

#include "ui/ClipGuard.h"

#include <SDL3/SDL_surface.h>

using micronotes::ui::ClipGuard;
using micronotes::ui::Rect;

namespace {

// A software renderer over a surface. No video subsystem, no window, no
// display: the clip rect is renderer state, and this is the cheapest renderer
// that has some.
struct Canvas {
  SDL_Surface* surface = nullptr;
  SDL_Renderer* renderer = nullptr;

  Canvas() {
    surface = SDL_CreateSurface(400, 300, SDL_PIXELFORMAT_ARGB8888);
    renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
  }

  ~Canvas() {
    if(renderer) SDL_DestroyRenderer(renderer);
    if(surface) SDL_DestroySurface(surface);
  }
};

SDL_Rect clipOf(SDL_Renderer* renderer) {
  SDL_Rect rect {};
  if(SDL_RenderClipEnabled(renderer)) SDL_GetRenderClipRect(renderer, &rect);
  return rect;
}

bool sameRect(const SDL_Rect& a, const SDL_Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

}

MICRONOTES_TEST(clip_guard_restores_no_clip_on_exit) {
  Canvas canvas;
  MICRONOTES_REQUIRE(canvas.renderer != nullptr);
  MICRONOTES_REQUIRE(!SDL_RenderClipEnabled(canvas.renderer));
  {
    const ClipGuard guard(canvas.renderer, Rect {10.0f, 20.0f, 100.0f, 50.0f});
    MICRONOTES_REQUIRE(SDL_RenderClipEnabled(canvas.renderer));
    MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {10, 20, 100, 50}));
  }
  // Nothing was clipped on the way in, so nothing is clipped on the way out.
  MICRONOTES_REQUIRE(!SDL_RenderClipEnabled(canvas.renderer));
}

MICRONOTES_TEST(clip_guard_restores_the_outer_clip_rather_than_clearing_it) {
  Canvas canvas;
  MICRONOTES_REQUIRE(canvas.renderer != nullptr);
  const ClipGuard panel(canvas.renderer, Rect {0.0f, 0.0f, 200.0f, 300.0f});
  {
    const ClipGuard field(canvas.renderer, Rect {10.0f, 10.0f, 80.0f, 20.0f});
    MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {10, 10, 80, 20}));
  }
  // The bug this guards against: the inner guard used to clear the clip, so
  // every draw after it in the panel's scope painted over the whole window.
  MICRONOTES_REQUIRE(SDL_RenderClipEnabled(canvas.renderer));
  MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {0, 0, 200, 300}));
}

MICRONOTES_TEST(clip_guard_intersects_with_the_clip_it_nests_in) {
  Canvas canvas;
  MICRONOTES_REQUIRE(canvas.renderer != nullptr);
  const ClipGuard pane(canvas.renderer, Rect {0.0f, 0.0f, 100.0f, 100.0f});
  {
    // A child rect computed in window coordinates can reach past its parent.
    // It must not be able to draw there.
    const ClipGuard row(canvas.renderer, Rect {50.0f, 50.0f, 200.0f, 200.0f});
    MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {50, 50, 50, 50}));
  }
  MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {0, 0, 100, 100}));
}

MICRONOTES_TEST(clip_guard_clips_everything_out_when_the_rects_do_not_meet) {
  Canvas canvas;
  MICRONOTES_REQUIRE(canvas.renderer != nullptr);
  const ClipGuard pane(canvas.renderer, Rect {0.0f, 0.0f, 100.0f, 100.0f});
  {
    const ClipGuard away(canvas.renderer, Rect {200.0f, 200.0f, 50.0f, 50.0f});
    // A scope with nothing visible in it clips to nothing, rather than
    // inheriting the whole window because the intersection was empty.
    MICRONOTES_REQUIRE(SDL_RenderClipEnabled(canvas.renderer));
    const SDL_Rect clip = clipOf(canvas.renderer);
    MICRONOTES_REQUIRE(clip.w == 0 && clip.h == 0);
  }
  MICRONOTES_REQUIRE(sameRect(clipOf(canvas.renderer), SDL_Rect {0, 0, 100, 100}));
}
