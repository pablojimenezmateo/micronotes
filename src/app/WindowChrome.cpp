#include "app/WindowChrome.h"

#include "app/Shell.h"

#include "ui/Metrics.h"

#include <cstddef>
#include <cmath>
#include <iostream>

namespace micronotes::app {
namespace {

using ui::contains;
using ui::ShellLayout;

SDL_HitTestResult resizeAt(bool left, bool right, bool top, bool bottom) {
  if(top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
  if(top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
  if(bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
  if(bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
  if(top) return SDL_HITTEST_RESIZE_TOP;
  if(bottom) return SDL_HITTEST_RESIZE_BOTTOM;
  if(left) return SDL_HITTEST_RESIZE_LEFT;
  if(right) return SDL_HITTEST_RESIZE_RIGHT;
  return SDL_HITTEST_NORMAL;
}

SDL_HitTestResult SDLCALL windowHitTest(SDL_Window* window, const SDL_Point* area, void* data) {
  auto* context = static_cast<HitTestContext*>(data);
  if(!area || !context || !context->ui || !context->renderer) return SDL_HITTEST_NORMAL;
  UiRuntime& ui = *context->ui;
  if(!ui.customChrome) return SDL_HITTEST_NORMAL;

  // The renderer draws under SDL_SetRenderScale, so the window coordinates SDL
  // hands over have to come back to the space the rects were recorded in.
  float x = 0.0f;
  float y = 0.0f;
  if(!SDL_RenderCoordinatesFromWindow(context->renderer, static_cast<float>(area->x),
                                      static_cast<float>(area->y), &x, &y)) {
    return SDL_HITTEST_NORMAL;
  }
  int windowW = 0;
  int windowH = 0;
  SDL_GetWindowSize(window, &windowW, &windowH);
  float right = 0.0f;
  float bottom = 0.0f;
  if(!SDL_RenderCoordinatesFromWindow(context->renderer, static_cast<float>(windowW),
                                      static_cast<float>(windowH), &right, &bottom)) {
    return SDL_HITTEST_NORMAL;
  }
  if(x < 0.0f || y < 0.0f || x >= right || y >= bottom) return SDL_HITTEST_NORMAL;

  const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
  if((flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) == 0) {
    const bool left = x < ui::kWindowFrameThickness;
    const bool rightEdge = x >= right - ui::kWindowFrameThickness;
    const bool top = y < ui::kWindowFrameThickness;
    const bool bottomEdge = y >= bottom - ui::kWindowFrameThickness;
    if(left || rightEdge || top || bottomEdge) return resizeAt(left, rightEdge, top, bottomEdge);
  }

  const ShellLayout layout = shellLayout(ui, static_cast<int>(std::lround(right)),
                                         static_cast<int>(std::lround(bottom)));
  if(!contains(layout.titleBar, x, y)) return SDL_HITTEST_NORMAL;
  // Everything in the strip that is a target keeps its click; the rest of it
  // moves the window. A press on a draggable region is consumed by the platform
  // to start that move and never reaches the app, which is why the maximize
  // button is the only way to maximize: there is no second click for a
  // double-click to pair with.
  if(contains(ui.favoriteButton, x, y)) return SDL_HITTEST_NORMAL;
  for(const auto& box : ui.windowButtons) {
    if(contains(box, x, y)) return SDL_HITTEST_NORMAL;
  }
  for(const auto& [rect, folder] : ui.crumbs) {
    (void)folder;
    if(contains(rect, x, y)) return SDL_HITTEST_NORMAL;
  }
  return SDL_HITTEST_DRAGGABLE;
}

}

bool installWindowHitTest(SDL_Window* window, SDL_Renderer* renderer, UiRuntime& ui,
                          HitTestContext& context) {
  context = HitTestContext {&ui, renderer};
  if(SDL_SetWindowHitTest(window, &windowHitTest, &context)) return true;
  // A borderless window with no hit test cannot be moved or resized at all, so
  // if the platform refuses one the decorations come back and the drawn buttons
  // stand down.
  std::cerr << "SDL_SetWindowHitTest failed: " << SDL_GetError() << "\n";
  ui.customChrome = false;
  SDL_SetWindowBordered(window, true);
  return false;
}

bool pressWindowButton(UiRuntime& ui, float x, float y, Uint8 button) {
  if(!ui.customChrome || button != SDL_BUTTON_LEFT) return false;
  static constexpr WindowAction kActions[] {WindowAction::Minimize, WindowAction::ToggleMaximize,
                                            WindowAction::Close};
  for(std::size_t i = 0; i < ui.windowButtons.size(); ++i) {
    if(!contains(ui.windowButtons[i], x, y)) continue;
    ui.pendingWindowAction = kActions[i];
    return true;
  }
  return false;
}

bool applyPendingWindowAction(SDL_Window* window, UiRuntime& ui, bool& running) {
  bool changed = false;
  if(ui.pendingWindowAction != WindowAction::None) {
    switch(ui.pendingWindowAction) {
      case WindowAction::Minimize: SDL_MinimizeWindow(window); break;
      case WindowAction::ToggleMaximize:
        if((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) SDL_RestoreWindow(window);
        else SDL_MaximizeWindow(window);
        break;
      case WindowAction::Close: running = false; break;
      case WindowAction::None: break;
    }
    ui.pendingWindowAction = WindowAction::None;
    changed = true;
  }
  // Read back rather than assumed: the compositor can maximize or restore the
  // window without being asked, and the middle button's glyph has to agree with
  // what actually happened.
  const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
  if(ui.windowMaximized != maximized) {
    ui.windowMaximized = maximized;
    changed = true;
  }
  return changed;
}

}
