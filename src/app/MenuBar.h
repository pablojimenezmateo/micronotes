#pragma once

#include "ui/Actions.h"
#include "ui/TextRenderer.h"
#include "ui/Menus.h"
#include "ui/Rect.h"

#include <optional>

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// The menu bar along the top of the window, and the popup an open menu drops.
//
// It replaces the icon rail that used to run down the leading edge. The rail
// held seven controls and was the only route back with every panel hidden,
// which made it load-bearing and, at seven, useless for anything else: every
// other command lived behind a chord or the palette, both of which have to be
// learnt before they can be used. A menu bar can be *read*.
//
// Every item is a `ui::ActionId` and nothing else, so an item cannot drift from
// the palette row and the key binding that claim to do the same thing. Where it
// sits is `ui::menuBarLayout`, so the paint, the two hit tests and the cursor
// shape cannot disagree about it, and the rule for a window too narrow to hold
// every menu can be tested without a renderer.
void drawMenuBar(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// The open menu's popup, drawn after every panel so it lands on top of them.
// Nothing at all when no menu is open.
void drawOpenMenu(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect bounds);

// What a press on the bar did. A menu opening or closing is handled in here --
// it is the bar's own state -- but an item chosen comes back as an action for
// `Application` to dispatch, because the command table lives there and the bar
// has no business reaching into it.
struct MenuBarClick {
  bool handled = false;
  std::optional<ui::ActionId> action;
};

// An open menu owns the press, whichever button it is: on one of its rows a
// left press is the command, and anywhere else -- or with any other button --
// the press is the dismissal and is spent on it. The bar answers for the whole
// window while it is open, which is why the button is its business and not the
// caller's.
MenuBarClick handleMenuBarClick(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect,
                                ui::Rect bounds, float x, float y, Uint8 button);

// Sliding along the bar with a menu open switches menus without a click, and
// sliding down an open popup moves the highlight. Returns whether the frame
// needs repainting.
bool handleMenuBarMotion(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, ui::Rect bounds,
                         float x, float y);

// Walking an open menu from the keyboard: arrows move, Enter chooses, Escape
// closes, Left and Right step to the neighbouring menu. `handled` says whether
// the key belonged to the menu at all, so the caller knows not to route it on.
struct MenuBarKey {
  bool handled = false;
  std::optional<ui::ActionId> action;
};

MenuBarKey handleMenuBarKey(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, ui::Rect bounds,
                            SDL_Keycode key);

// Opening a menu without the pointer: `Alt` and the letter the bar underlines,
// or `F10` for the first one. Returns whether the key was one of those.
//
// An open menu was already fully navigable from the keyboard -- the arrows walk
// it, Left and Right step to the neighbour, Enter chooses, Escape shuts it --
// and there was no way to *open* one at all. That is half of the bar's own
// argument given back: it exists because a shell whose only routes to a command
// are a chord and a palette is one where every command has to be learnt before
// it can be used, and a bar you can only reach by taking a hand off the
// keyboard is not read, it is pointed at.
//
// `Alt` stays an ordinary modifier here, which is what keeps this a branch
// rather than a keyup-driven state machine: micronotes binds `Ctrl+Alt+Left`
// and `Ctrl+Alt+Right`, so what has to be told apart is `Alt+F` from
// `Ctrl+Alt+F` -- and `Ctrl` is in the mask. Nothing is bound to a *bare* `Alt`
// press, so nothing has to notice one.
bool openMenuByKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift, bool alt);

// Whether the pointer is over something clickable, so the cursor can say so,
// and whether it is over a part of the bar a borderless window may be dragged
// by -- which is everything the menus and the window controls left.
bool menuBarHasControlAt(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y);

// Whether an item is ticked. A question about the shell rather than about the
// table, which is why it is here and not in `ui::Menus`.
bool menuItemChecked(const UiRuntime& ui, ui::ActionId action);

// Whether an item can be chosen at all. `needsNote` plus the handful of items
// that need something more specific than a note.
bool menuItemEnabled(const UiRuntime& ui, ui::ActionId action);

}
