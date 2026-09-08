#pragma once

#include "CoreAliases.h"

#include "core/editor/TextField.h"

#include <SDL3/SDL.h>

// What a key means to each of the four things that can have the keyboard.
//
// `app/KeyRouter.h` decides *which* of them the key belongs to -- an open
// overlay, a chord the binding table runs, a focused field, a block selection,
// the caret, the tree -- and these are what it hands off to. Splitting them out
// is what `../microide` does with its own key coordinator, which is four files
// (`...Editor`, `...Sidebar`, `...Modal`, `...Settings`) behind one router, and
// for the same reason: the router's job is the *order*, and each surface's job
// is a closed set of keys with nothing to say about the others.
//
// Each of these is entered only when the router has already established that
// this surface owns the key, so none of them re-checks the focus.
namespace micronotes::app {

struct UiRuntime;

void handleFieldKey(UiRuntime& ui, editor::TextField& field, SDL_Keycode key, bool ctrl,
                    bool shift);
void handleBlockSelectionKey(UiRuntime& ui, SDL_Keycode key, bool shift, bool alt);
void handleEditorKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift, bool alt);
void handleSidebarKey(UiRuntime& ui, SDL_Keycode key);

}
