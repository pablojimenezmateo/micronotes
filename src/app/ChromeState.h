#pragma once

#include "ui/Menus.h"
#include "ui/Rect.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

// The window's own furniture: the menu bar, the controls at its trailing edge,
// and the breadcrumb trail over the page.
//
// Twelve loose fields on `UiRuntime` before this, and what named them together
// is a single unusual constraint they all answer to. This window is borderless
// and draws its own controls, so the platform asks it -- on a callback, with no
// renderer in scope -- which parts of the top strip are draggable. Nothing on
// that callback can measure a label, so every target in the bar has to be
// *recorded as it is drawn* rather than recomputed on demand: the menu run, the
// overflow chevron, the three buttons. That is why the rects are state at all,
// and grouping them is what makes the reason legible instead of repeated once
// per field.
namespace micronotes::app {

// What the drawn window controls ask the run loop to do.
//
// A request rather than a call, because the buttons are drawn and hit-tested in
// shell code that has no business knowing about `SDL_Window`; the run loop,
// which owns the window, acts on it at the end of the frame.
enum class WindowAction {
  None,
  Minimize,
  ToggleMaximize,
  Close
};

struct ChromeState {
  // Which menu on the bar is open, and which of its rows the keyboard is on.
  // The pointer owns the highlight while it is inside the popup, so a walk with
  // the arrows followed by a mouse move does not leave two rows lit.
  ui::MenuId openMenu = ui::MenuId::None;
  std::size_t menuHighlight = 0;

  // Last frame's menu bar, so the popup -- which is drawn after every panel, on
  // top of them -- can find the item it hangs off without being handed the
  // whole layout.
  ui::Rect menuBarRect;
  // The bar's own targets, recorded as they are drawn: the run the menus
  // occupy, and the overflow chevron. Everything in the bar that is *not* one
  // of these, or one of the buttons below, is the strip the window is dragged
  // by -- which is the question the platform's hit test asks.
  ui::Rect menuItemsBand;
  ui::Rect menuChevron;
  // Minimise, maximise, close -- in that order.
  std::array<ui::Rect, 3> windowButtons {};

  // The breadcrumb trail over the page, recorded as it is drawn: a crumb is a
  // folder to jump to, and the star at the end pins the note.
  std::vector<std::pair<ui::Rect, std::filesystem::path>> crumbs;
  ui::Rect pinButton;

  WindowAction pendingWindowAction = WindowAction::None;
  // Whether this window draws its own controls instead of wearing the
  // compositor's. Cleared when the platform refuses a hit test, because a
  // borderless window nobody can move is worse than a decorated one.
  bool customChrome = true;
  // Whether the window is maximised, so the middle button can draw the restore
  // glyph instead. Tracked from window events rather than queried in the draw,
  // which would ask the display server a question every frame.
  bool windowMaximized = false;
};

}
