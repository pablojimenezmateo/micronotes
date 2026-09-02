#include "app/Screenshot.h"

#include <iostream>

#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

namespace micronotes::app {

int captureWindowToFile(SDL_Renderer* renderer, const std::filesystem::path& path,
                        int fallbackWidth, int fallbackHeight,
                        const std::function<void(int, int)>& drawFrame) {
  // Pump enough events for the compositor to map and size the window before the
  // pixels are read back; an unmapped window reads back blank.
  for(int i = 0; i < 60; ++i) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {}
    int width = fallbackWidth;
    int height = fallbackHeight;
    SDL_GetWindowSize(renderer ? SDL_GetRenderWindow(renderer) : nullptr, &width, &height);
    if(drawFrame) drawFrame(width, height);
    SDL_Delay(8);
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
