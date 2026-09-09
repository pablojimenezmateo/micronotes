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
    SDL_SetCursor(defaultCursor);
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

  void apply(CursorKind kind) {
    if(kind == active) return;
    if(SDL_Cursor* next = cursor(kind)) {
      SDL_SetCursor(next);
      active = kind;
    }
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
