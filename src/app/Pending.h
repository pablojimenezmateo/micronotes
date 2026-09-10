#pragma once

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// Everything that can change between two events.
//
// The loop wakes on input, but several things move without any input behind
// them: a window action a surface queued, a change another program made to the
// library, the caret's blink, a PDF whose destination has come back from the
// desktop's file chooser on a thread of its own, and the autosave clock. Each
// of them arrived as one more `if` in the event loop, and the loop is supposed
// to be the window, the renderer and the wait -- which is what the line budget
// `ArchitectureTests` holds `Application.cpp` to is there to say.
//
// So they are a list, in one place, in the order they have to run: the window
// action first, because it can end the session and there is no point doing
// work for a frame that will not be drawn; the library next, because a note
// reloaded underneath the autosave must not then be written back from a stale
// buffer.
//
// Returns whether anything changed, which the caller reads as "repaint".
bool applyPendingWork(SDL_Window* window, UiRuntime& ui, bool& running, Uint64 nowMs);

}
