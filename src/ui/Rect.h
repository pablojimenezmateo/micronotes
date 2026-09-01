#pragma once

#include <SDL3/SDL_rect.h>

#include <cmath>

namespace micronotes::ui {

// The rectangle every pane, row and hit target is expressed in, in logical
// pixels with the origin at the top left of the window.
//
// It lives in its own header rather than in Draw.h so that geometry can be
// computed and unit-tested without pulling in a renderer, a font store or a
// texture cache: the shell layout is a pure function, and nothing about it
// needs to know how a pixel is painted.
struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;

  friend bool operator==(const Rect&, const Rect&) = default;
};

inline bool contains(const Rect& rect, float x, float y) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

inline bool empty(const Rect& rect) {
  return rect.w <= 0.0f || rect.h <= 0.0f;
}

inline SDL_FRect sdlRect(const Rect& rect) {
  return {rect.x, rect.y, rect.w, rect.h};
}

inline SDL_Rect clipRect(const Rect& rect) {
  return {
    static_cast<int>(std::floor(rect.x)),
    static_cast<int>(std::floor(rect.y)),
    static_cast<int>(std::ceil(rect.w)),
    static_cast<int>(std::ceil(rect.h)),
  };
}

}
