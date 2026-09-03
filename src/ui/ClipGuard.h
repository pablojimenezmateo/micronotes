#pragma once

#include "ui/Rect.h"

#include <SDL3/SDL_render.h>

// The renderer's clip rect, scoped.
//
// Its own header rather than part of `ui/Draw.h` because it needs a renderer
// and a rectangle and nothing else -- no font store, no texture cache, no
// palette -- which is what lets it be unit-tested against a software renderer.
namespace micronotes::ui {

// Clips every draw in the scope to `rect`, intersected with whatever clip was
// already standing, and puts that outer clip back on the way out.
//
// Both halves of that are the point, and the guard this replaces had neither.
//
// **Restore, not clear.** The old destructor set the clip to `nullptr`, which
// disables clipping outright rather than restoring what it found. So an inner
// guard's exit dropped an outer guard's clip and every draw after it in the
// outer scope painted unclipped. That was not hypothetical: `drawSidebar` held
// a guard for the panel and then called the search field, whose own guard
// cleared it -- so the scope toggle, its label and the whole empty-state
// message were drawn with no clip at all, and the first thing tall enough to
// need one would have painted over the tab strip.
//
// **Intersect, not replace.** A pane's guard has to be able to trust that its
// children cannot draw outside it. Replacing the clip means a child's rect,
// which is computed in window coordinates and may extend past its parent, wins
// over the parent that contains it.
//
// Two SDL details make the naive version wrong even when it restores:
// `SDL_GetRenderClipRect` reports whether the *call* worked, not whether a clip
// is active -- it fills an empty rect when clipping is off -- so
// `SDL_RenderClipEnabled` is what decides; and `SDL_SetRenderClipRect` *enables*
// a zero-area clip for a `{0,0,0,0}` rect, where only a null pointer disables
// clipping. Restoring a saved-but-empty rect would therefore blank everything
// drawn afterwards.
class ClipGuard {
public:
  ClipGuard(SDL_Renderer* renderer, Rect rect) : renderer_(renderer) {
    outerEnabled_ = SDL_RenderClipEnabled(renderer_);
    SDL_Rect clip = clipRect(rect);
    if(outerEnabled_) {
      SDL_GetRenderClipRect(renderer_, &outer_);
      // An empty intersection is a zero-area clip, which is the honest answer:
      // this scope has nothing visible to draw in.
      if(!SDL_GetRectIntersection(&outer_, &clip, &clip)) clip = SDL_Rect {outer_.x, outer_.y, 0, 0};
    }
    SDL_SetRenderClipRect(renderer_, &clip);
  }

  ~ClipGuard() {
    SDL_SetRenderClipRect(renderer_, outerEnabled_ ? &outer_ : nullptr);
  }

  ClipGuard(const ClipGuard&) = delete;
  ClipGuard& operator=(const ClipGuard&) = delete;

private:
  SDL_Renderer* renderer_ = nullptr;
  bool outerEnabled_ = false;
  SDL_Rect outer_ {};
};

}
