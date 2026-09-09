#include "app/EventRouter.h"

#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/KeyRouter.h"
#include "app/Layout.h"
#include "app/MenuBar.h"
#include "app/Notes.h"
#include "app/PointerRouter.h"
#include "app/Scroll.h"
#include "app/Shell.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/Actions.h"
#include "ui/Menus.h"

#include <string>

namespace micronotes::app {
namespace {

// The shape the pointer takes, for the window as it stands. Settled after every
// pointer event; see the note on `SystemCursors::apply` for why a failed set is
// not recorded as a set.
void settleCursor(ui::TextRenderer& text, UiRuntime& ui, SystemCursors& cursors, int width,
                  int height, bool reassert = false) {
  cursors.apply(classifyCursor(text, ui, width, height), reassert);
}

// An open menu owns the keyboard first: arrows walk it, Enter chooses, Escape
// shuts it. Anything else closes it and falls through, so typing with a menu
// accidentally open does not silently go nowhere.
//
// Here rather than inside `handleKey` because the walk needs the bar's geometry
// and the face it was measured in, and `handleKey` has neither -- threading
// both through it would have every other branch carry them for the one that
// uses them.
bool openMenuTookKey(ui::TextRenderer& text, UiRuntime& ui, SDL_Keycode key, int width,
                     int height) {
  if(ui.chrome.openMenu == ui::MenuId::None) return false;
  const ShellLayout layout = shellLayout(ui, width, height);
  const MenuBarKey menu = handleMenuBarKey(
    text, ui, layout.menuBar, {0, 0, static_cast<float>(width), static_cast<float>(height)}, key);
  if(menu.action) {
    if(const auto* spec = ui::findAction(*menu.action)) performCommand(ui, std::string(spec->name));
  }
  return menu.handled;
}

}

EventOutcome routeEvent(const SDL_Event& event, ui::TextRenderer& text, UiRuntime& ui,
                        SystemCursors& cursors, int width, int height) {
  EventOutcome outcome;
  switch(event.type) {
    case SDL_EVENT_QUIT:
      outcome.quit = true;
      return outcome;

    case SDL_EVENT_TEXT_INPUT:
      perf::addCounter(perf::CounterId::InputTextEvents);
      handleText(ui, event.text.text);
      return outcome;

    case SDL_EVENT_KEY_DOWN:
      perf::addCounter(perf::CounterId::InputKeyEvents);
      if(!openMenuTookKey(text, ui, event.key.key, width, height)) {
        handleKey(ui, event.key.key, event.key.scancode, event.key.mod);
      }
      return outcome;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      ui.pointer.x = event.button.x;
      ui.pointer.y = event.button.y;
      handleMouse(text, ui, event.button.x, event.button.y, event.button.button, width, height);
      settleCursor(text, ui, cursors, width, height);
      return outcome;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      ui.pointer.x = event.button.x;
      ui.pointer.y = event.button.y;
      handleMouseUp(ui, event.button.x, event.button.y, event.button.button);
      settleCursor(text, ui, cursors, width, height);
      return outcome;

    case SDL_EVENT_MOUSE_MOTION:
      ui.pointer.x = event.motion.x;
      ui.pointer.y = event.motion.y;
      handleMouseMotion(text, ui, event.motion.x, event.motion.y, width, height);
      settleCursor(text, ui, cursors, width, height);
      return outcome;

    case SDL_EVENT_MOUSE_WHEEL:
      routeWheel(ui, event.wheel.y, width, height);
      return outcome;

    case SDL_EVENT_DROP_FILE:
      if(event.drop.data) attachPathToEditor(ui, event.drop.data);
      return outcome;

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
      outcome.displayScaleChanged = true;
      return outcome;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      outcome.displayScaleChanged = true;
      ui.pacing.noteResize(SDL_GetTicks());
      return outcome;

    case SDL_EVENT_WINDOW_EXPOSED:
      // A region of the window has become undefined and is ours to fill. Same
      // urgency as a resize step, and on some window managers the only event a
      // resize produces at all.
      ui.pacing.noteResize(SDL_GetTicks());
      return outcome;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      rescanLibraryAfterExternalChange(ui);
      return outcome;

    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
      ui.pacing.noteResize(SDL_GetTicks());
      [[fallthrough]];
    case SDL_EVENT_WINDOW_MOVED:
      // A compositor-driven resize or move paints its own grab cursor and does
      // not tell SDL, so the shape on screen and SDL's idea of it disagree --
      // and the pointer can come to rest without a motion event to correct it.
      // Re-assert rather than trusting the last shape asked for.
      settleCursor(text, ui, cursors, width, height, true);
      return outcome;

    default:
      return outcome;
  }
}

}
