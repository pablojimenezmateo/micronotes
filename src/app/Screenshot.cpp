#include "app/Screenshot.h"

#include <iostream>

#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

namespace micronotes::app {

// Frames drawn before the pixels are read back.
//
// Sixty, and the number is load-bearing twice over. It gives the compositor
// time to map and size the window -- an unmapped window reads back blank. And
// it is the *workload*: every table in `docs/performance.md` is "Release,
// headless, 60 frames", read out of the counters a capture leaves behind, with
// the per-frame figures divided by it. A capture that drew as few frames as it
// could get away with would be quick and would measure nothing, and an
// idle-frame regression -- the sidebar rebuilding per frame, the fold predicate
// asked per block per frame -- is exactly what this instrument is for.
constexpr int kCaptureFrames = 60;

int captureWindowToFile(SDL_Renderer* renderer, const std::filesystem::path& path,
                        int fallbackWidth, int fallbackHeight,
                        const std::function<void(int, int)>& drawFrame) {
  // Vsync off, for the frames nobody will see.
  //
  // The app runs with it on, because a present that beats the display is a
  // frame thrown away. A capture has no viewer, so pacing its frames to a
  // refresh interval is sixty waits for nothing -- and that wait was the whole
  // of the capture's wall clock: 1.10 s a run, of which 0.50 s was
  // `frame.present_micros`.
  //
  // This loop used to carry an `SDL_Delay(8)` per frame as well, on a comment
  // about giving the compositor time. Removing *that* measures out at exactly
  // zero, which is worth writing down because it looks like the obvious win:
  // the present blocks until the next refresh either way, so a frame that slept
  // first simply waits 8 ms less in `SDL_RenderPresent`. 1.09 s a run without
  // it. Vsync was the thing holding the clock, and 60 presents to an unpaced
  // renderer are 0.24 s.
  SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_DISABLED);
  for(int i = 0; i < kCaptureFrames; ++i) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {}
    int width = fallbackWidth;
    int height = fallbackHeight;
    SDL_GetWindowSize(renderer ? SDL_GetRenderWindow(renderer) : nullptr, &width, &height);
    if(drawFrame) drawFrame(width, height);
  }

  SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr);
  if(!frame) {
    std::cerr << "SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
    return 1;
  }

  bool saved = false;
#if MICRONOTES_HAS_SDL3_IMAGE
  saved = IMG_SavePNG(frame, path.c_str());
#endif
  if(!saved) saved = SDL_SaveBMP(frame, path.c_str());
  SDL_DestroySurface(frame);
  if(!saved) {
    std::cerr << "Saving screenshot failed: " << SDL_GetError() << "\n";
    return 1;
  }
  std::cout << "wrote " << path.string() << "\n";
  return 0;
}

}
