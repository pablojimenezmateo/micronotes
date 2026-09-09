#pragma once

#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes {
namespace ui {
class TextRenderer;
}

namespace app {

struct UiRuntime;

// What shape the pointer can take.
//
// Here with the two things that answer for it and apply it, rather than in
// `app/Shell.h` with the shell's state. This header used to forward-declare it
// -- `enum class CursorKind;` -- which is the tell: the file whose whole
// subject is the cursor could not name the cursor's own type without the
// runtime, so `classifyCursor` returned a value its own caller had to include
// the shell to switch on.
enum class CursorKind {
  Default,
  Text,
  Pointer,
  ResizeHorizontal,
  ResizeVertical
};

// The five system cursors, owned for the life of the window.
//
// `apply` is what keeps this from being five loose `SDL_Cursor*`: it compares
// against the shape already set and does nothing when they agree, so the
// pointer-motion path does not call into the window system sixty times a
// second to set the cursor it is already showing.
struct SystemCursors {
  SDL_Cursor* defaultCursor = nullptr;
  SDL_Cursor* text = nullptr;
  SDL_Cursor* pointer = nullptr;
  SDL_Cursor* resizeHorizontal = nullptr;
  SDL_Cursor* resizeVertical = nullptr;
  CursorKind active = CursorKind::Default;

  bool init() {
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    resizeHorizontal = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    resizeVertical = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    if(!defaultCursor || !text || !pointer || !resizeHorizontal || !resizeVertical) return false;
    if(!SDL_SetCursor(defaultCursor)) return false;
    active = CursorKind::Default;
    return true;
  }

  void destroy() {
    if(defaultCursor) SDL_DestroyCursor(defaultCursor);
    if(text) SDL_DestroyCursor(text);
    if(pointer) SDL_DestroyCursor(pointer);
    if(resizeHorizontal) SDL_DestroyCursor(resizeHorizontal);
    if(resizeVertical) SDL_DestroyCursor(resizeVertical);
    defaultCursor = nullptr;
    text = nullptr;
    pointer = nullptr;
    resizeHorizontal = nullptr;
    resizeVertical = nullptr;
  }

  SDL_Cursor* cursor(CursorKind kind) const {
    switch(kind) {
      case CursorKind::Text: return text;
      case CursorKind::Pointer: return pointer;
      case CursorKind::ResizeHorizontal: return resizeHorizontal;
      case CursorKind::ResizeVertical: return resizeVertical;
      case CursorKind::Default:
      default: return defaultCursor;
    }
  }

  // Puts `kind` on screen, and records it only if that actually happened.
  //
  // Three things here are load-bearing and all three are Wayland, where a
  // cursor is a client-side surface rather than a server-side shape:
  //
  //  * `SDL_SetCursor`'s result is checked. It used to be discarded and
  //    `active` assigned regardless, so a set that failed was recorded as a
  //    set that worked -- and because the next call short-circuits on `kind ==
  //    active`, one failure convinced the shell that shape was already showing
  //    and it never tried again for the life of the window.
  //  * the nudge. `SDL_SetCursor` no-ops internally when handed the handle SDL
  //    already holds, and SDL's Wayland hit-test path can change the
  //    *displayed* cursor without updating that handle -- which micronotes is
  //    exposed to because its window is borderless and carries a hit test. The
  //    displayed shape and SDL's idea of it then disagree, and setting our
  //    unchanged handle is swallowed. Passing through a different cursor first
  //    means the real set cannot be. `../microide` found this one; see its
  //    `dev-docs/platform/wayland-stale-cursor.md`.
  //  * `reassert`, for the callers that know the compositor may have taken the
  //    cursor from under us: a resize, a restore, a maximise. Those arrive
  //    with the pointer possibly stationary, so there is no motion event
  //    coming to correct it.
  void apply(CursorKind kind, bool reassert = false) {
    if(kind == active && !reassert) return;
    SDL_Cursor* next = cursor(kind);
    if(!next) return;
    if(reassert) {
      if(SDL_Cursor* nudge = next == defaultCursor ? text : defaultCursor;
         nudge && nudge != next) {
        SDL_SetCursor(nudge);
      }
    }
    if(SDL_SetCursor(next)) active = kind;
  }
};

// What shape the pointer takes, for the window as it stands this frame.
//
// One function, and it has to stay one: the answer is a priority order --
// a drag in progress beats a resize gutter, a gutter beats the panel beside it,
// a scrollbar beats the row lying under it -- and an order spread over the
// surfaces it ranks is an order nobody can read. Every surface it asks is a
// `...HasControlAt` predicate derived from the same geometry that surface drew
// and clicks against, so the cursor cannot promise a click that would miss.
//
// It lived in Application.cpp, where it was the largest thing in the file that
// had nothing to do with running a window.
CursorKind classifyCursor(ui::TextRenderer& text, UiRuntime& ui, int width, int height);

}
}
