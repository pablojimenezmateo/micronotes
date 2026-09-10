#pragma once

#include "app/Cursor.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

// The SDL resources one run of the app owns: the video subsystem, the window,
// its renderer, the system cursors and the window's text input.
//
// A type rather than five locals in `run()`, for two reasons that are both
// about the way out rather than the way in.
//
// The first is that there were four of them. `run()` had a teardown ladder
// after the window failed, another after the renderer failed, a third on the
// `--screenshot` path and a fourth at the end of the loop -- four spellings of
// the same sequence, three of them partial by necessity because they run at
// different depths. `../microide` has the bug that shape produces written down
// as TD-2026-07-16-42: a failure *after* the window was created left the window
// and the renderer behind, because the handler for it was written for the state
// two lines earlier. Every way out of this function is now a return.
//
// The second is destruction order, which the ladder had wrong. `SDL_Renderer`
// owns every texture made against it, so the caches holding those textures --
// `ui::TextRenderer`'s glyph cache and `ui::ImageCache` -- have to be gone
// before it is. `run()` destroyed the renderer by hand at the bottom of the
// function while both were still alive, so their destructors then called
// `SDL_DestroyTexture` on textures SDL had already freed with the renderer. It
// survived on SDL's own magic check reading the freed struct and refusing,
// which is not a thing to depend on. Declare this *before* the text renderer
// and the image cache and the language settles it: they are destroyed first.
namespace micronotes::app {

class AppWindow {
public:
  AppWindow() = default;
  ~AppWindow();

  AppWindow(const AppWindow&) = delete;
  AppWindow& operator=(const AppWindow&) = delete;

  // Initialises SDL, creates the window unmapped, makes its renderer and takes
  // the system cursors. False when one of those failed, having said which on
  // stderr; whatever had already been taken is released by the destructor
  // rather than here, so a new failure cannot be added without one.
  bool open(int width, int height);

  SDL_Window* window() const {
    return window_;
  }

  SDL_Renderer* renderer() const {
    return renderer_;
  }

  SystemCursors& cursors() {
    return cursors_;
  }

  // Re-reads the display scale and pushes it to the renderer and the face
  // cache. `requested` is `--scale`; anything <= 0 asks the display.
  //
  // Layout is in logical units and `SDL_GetWindowSize` reports the same, so one
  // render scale is all HIGH_PIXEL_DENSITY needs to produce a sharper image
  // rather than a bigger one: layout stays logical, SDL scales it up, and glyph
  // textures are drawn at their own physical size so they stay sharp. Expressed
  // as a scale rather than a ratio against a fixed window size, it stays
  // correct across resizes -- and a display-changed event re-reads it when the
  // window moves to a monitor with a different one.
  //
  // Rebuilding the faces throws the glyph cache away, so this is guarded on a
  // real change rather than called per frame. It is here rather than in the
  // loop because the renderer's scale and the face cache that has to be
  // dropped with it are both this object's.
  void applyDisplayScale(ui::TextRenderer& text, float requested);

private:
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SystemCursors cursors_;
  bool sdlOpen_ = false;
  bool textInput_ = false;
  float appliedScale_ = 0.0f;
};

}
