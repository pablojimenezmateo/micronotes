#pragma once

#include <SDL3/SDL.h>

// What a keystroke means, given where the focus is.
//
// Two entry points, because SDL delivers a keystroke twice: once as a key with
// modifiers, and once as the text it produced. `handleText` is the typing path
// and `handleKey` is everything else.
//
// `handleKey` is a chain, and the order of its arms *is* the design: an open
// overlay takes the key first, then the chords that mean the same thing
// everywhere, then the focus-dependent branches -- a field, a block selection,
// the caret, the tree -- each of which reads the focus, the selection or the
// pane before deciding what the key meant. That is why the chain is not a
// table: `ui::findActionForChord` already handles the bindings whose meaning is
// context-free, and `kBoundHere` in the implementation lists the ones that have
// made it over.
//
// It is here rather than in Application.cpp because a key handler is a decision
// tree over the shell's state and nothing else -- no renderer, no window, no
// event loop -- which makes it the most testable thing in the shell and, at
// three hundred lines inside the file that also drew the frame, the least
// tested.
namespace micronotes::app {

struct UiRuntime;

// Typed text: into the focused field, or into the note.
void handleText(UiRuntime& ui, const char* input);

// A key press. `mod` is the event's modifier state, which is merged with the
// live one: a chord assembled fast enough can arrive with the event's copy
// already stale.
void handleKey(UiRuntime& ui, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod);

}
