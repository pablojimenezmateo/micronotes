#include "app/AppWindow.h"

#include "app/WindowChrome.h"
#include "ui/Painter.h"

#include <cmath>
#include <iostream>

namespace micronotes::app {

bool AppWindow::open(int width, int height) {
  // Before SDL_Init, because that is when the video backend reads them.
  setInputHints();
  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }
  sdlOpen_ = true;

  window_ = createAppWindow(width, height);
  if(!window_) return false;

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if(!renderer_) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    return false;
  }
  ui::configureRenderer(renderer_);

  // Present in step with the display. Without this the renderer tears on one
  // frame and stalls on the next, which reads as jitter even when every frame
  // is well inside budget.
  SDL_SetRenderVSync(renderer_, 1);
  SDL_StartTextInput(window_);
  textInput_ = true;

  // Not fatal: an app whose pointer never changes shape is worse than this one
  // and better than no app, so this reports and carries on. The cursor code
  // handles a null handle -- see `app/CursorTheme.h` for the other reason it
  // has to.
  if(!cursors_.init()) {
    std::cerr << "SDL_CreateSystemCursor failed: " << SDL_GetError() << "\n";
  }
  return true;
}

void AppWindow::applyDisplayScale(ui::TextRenderer& text, float requested) {
  float scale = requested > 0.0f ? requested : SDL_GetWindowDisplayScale(window_);
  if(scale <= 0.0f) scale = 1.0f;
  if(std::abs(scale - appliedScale_) < 0.01f) return;
  appliedScale_ = scale;
  text.setDisplayScale(scale);
  SDL_SetRenderScale(renderer_, scale, scale);
}

// Reverse order, and each step guarded, because this also runs after a failed
// `open`: the renderer may not exist, and neither may the window.
AppWindow::~AppWindow() {
  if(textInput_ && window_) SDL_StopTextInput(window_);
  cursors_.destroy();
  if(renderer_) SDL_DestroyRenderer(renderer_);
  if(window_) SDL_DestroyWindow(window_);
  if(sdlOpen_) SDL_Quit();
}

}
