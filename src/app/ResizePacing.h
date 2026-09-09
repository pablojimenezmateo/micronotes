#pragma once

#include <SDL3/SDL.h>

// Vsync is what a *viewer* wants, and a window being dragged by its edge is not
// one yet.
//
// While a resize is in flight the window's geometry belongs to the window
// manager and its contents belong to micronotes, and the two are only ever as
// close as the next present. The region the window grew into holds undefined
// content until then, which is what reads as a flicker to black for the length
// of the drag -- and with vsync on, "until then" is a refresh away. Measured
// over ten scripted `xdotool windowsize` steps on a 1000x700 window: 20
// presents, 21,845 us of drawing against 471,173 us of presenting. The draw is
// 1.1 ms a frame and the present is 23.6 ms, so 95% of a resize step is the
// app waiting for a display it is behind.
//
// So the drag is unpaced and everything else is paced. Tearing during a drag is
// not a cost worth naming next to a window that has not been painted at all,
// and the moment the drag settles the pacing comes back -- a present that beats
// the display is a frame thrown away, which is why vsync is on the rest of the
// time.
//
// This is not the retained scene texture `../microide` has. That one exists to
// make a *partial* redraw cheap, and its own resize handling is this: fall back
// while the drag is settling rather than reallocate a GPU target per event.
// micronotes presents the whole window every frame, so the texture would buy it
// nothing and this is the half that was worth taking.
namespace micronotes::app {

class ResizePacing {
public:
  // How long after the last resize event the drag is still considered live.
  // Long enough to cover the gap between two steps of a drag, short enough that
  // an ordinary frame after one is paced again.
  static constexpr Uint64 kSettleMs = 250;

  void noteResize(Uint64 nowMs) { lastResizeMs_ = nowMs; }

  // Whether the frame about to be presented should wait for the display.
  bool paced(Uint64 nowMs) const {
    return lastResizeMs_ == 0 || nowMs - lastResizeMs_ > kSettleMs;
  }

  // Puts the renderer in the state `paced` asks for. Called once a frame, just
  // before the present; the SDL call is made only on the two edges, because
  // setting a mode is not free on every backend and this is the frame path.
  //
  // Returns whether it changed anything, which is what the counter posts: two
  // per resize drag, off when it begins and on when it settles. A number that
  // grows with the frame count means something is calling a resize a frame.
  bool settle(SDL_Renderer* renderer, Uint64 nowMs) {
    const bool wanted = paced(nowMs);
    if(wanted == applied_) return false;
    applied_ = wanted;
    SDL_SetRenderVSync(renderer, wanted ? 1 : SDL_RENDERER_VSYNC_DISABLED);
    return true;
  }

  // A capture is not a session: it has no viewer, so it is never paced, and it
  // owns the renderer's vsync for its whole run. Told rather than asked, so the
  // next ordinary frame does not put the display back under it.
  void noteUnpacedByCaller() { applied_ = false; }

private:
  Uint64 lastResizeMs_ = 0;
  // Matches what `Application` sets before the loop starts, so the first frame
  // does not have to touch the renderer to agree with it.
  bool applied_ = true;
};

}
