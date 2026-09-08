#include "CoreAliases.h"
#include "app/Application.h"

#include "app/Chrome.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Cursor.h"
#include "app/Frame.h"
#include "app/FramePolicy.h"
#include "app/FrameTrace.h"
#include "app/KeyRouter.h"
#include "app/MenuBar.h"
#include "app/Notes.h"
#include "app/PointerRouter.h"
#include "app/Scroll.h"
#include "app/SessionState.h"
#include "app/Shell.h"
#include "app/Startup.h"
#include "app/WindowChrome.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/Fonts.h"
#include "ui/Menus.h"
#include "ui/Metrics.h"
#include "ui/ShellLayout.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace micronotes::app {
// The loop's own vocabulary. Everything else it needs is a named unit: the
// frame is `app/Frame.h`, the input routers are `app/KeyRouter.h` and
// `app/PointerRouter.h`, and what the command line asked for is
// `app/Startup.h`. What is left here is the window, the renderer and the wait.
namespace {

using micronotes::ui::ImageCache;
using micronotes::ui::Rect;
using micronotes::ui::ShellLayout;
using micronotes::ui::TextRenderer;

}

int run(ApplicationOptions options) {
  // Names the thread whose latency the user feels, so the trace summaries can
  // rank a 30 ms frame above a 200 ms background rescan that blocks nobody.
  perf::markMainThread();
  // Every way out reports, not only the one through the event loop: --screenshot
  // returns after writing its file and --headless returns before a window
  // exists, and a dump wired into the loop alone is silent for both.
  perf::dumpAtExit();
  dumpFrameTraceAtExit();
  const microcore::perf::StartupScope startup("startup");
  UiRuntime ui;
  if(!applyStartupOptions(ui, options)) return 1;
  // Everything above is state, so this is a complete run for anything that does
  // not draw: the tests and the perf harness stop here.
  if(options.headless) return 0;

  setInputHints();
  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  if(options.theme) ui::setThemeMode(*options.theme);

  SDL_Window* window = createAppWindow(options.windowWidth, options.windowHeight);
  if(!window) {
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if(!renderer) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  ui::configureRenderer(renderer);

  // Held for the lifetime of the window: SDL keeps the pointer and calls back
  // into it on every pointer press near the frame.
  HitTestContext hitTestContext;
  installWindowHitTest(window, renderer, ui, hitTestContext);

  // Present in step with the display. Without this the renderer tears on one
  // frame and stalls on the next, which reads as jitter even when every frame
  // is well inside budget.
  SDL_SetRenderVSync(renderer, 1);

  SDL_StartTextInput(window);
  installWatcherWake(ui);
  TextRenderer text(renderer);
  // Every layout number in this file is in logical (density-independent) units,
  // and SDL_GetWindowSize reports the same, so one render scale is all that
  // HIGH_PIXEL_DENSITY needs to produce a sharper image rather than a bigger
  // one. Layout stays logical, SDL scales it up, and glyph textures are drawn at
  // their own physical size so they stay sharp. Expressed as a scale rather than
  // a ratio against a fixed window size, it stays correct across resizes; the
  // display-changed event below re-reads it when the window moves to a monitor
  // with a different scale.
  //
  // A second lambda used to seed the same render scale from the window's pixel
  // density just above here; nothing called it twice and this one overwrote it
  // unconditionally, so it was a density that never reached a frame.
  float appliedScale = 0.0f;
  auto applyDisplayScale = [&]() {
    float scale = options.scale > 0.0f ? options.scale : SDL_GetWindowDisplayScale(window);
    if(scale <= 0.0f) scale = 1.0f;
    if(std::abs(scale - appliedScale) < 0.01f) return;
    appliedScale = scale;
    text.setDisplayScale(scale);
    SDL_SetRenderScale(renderer, scale, scale);
  };
  applyDisplayScale();
  if(inputDebugEnabled()) {
    std::cerr << "fonts source=\"" << text.fonts().sourceDescription() << "\""
              << " ready=" << text.fonts().ready() << "\n";
  }
  ImageCache images(renderer);
  SystemCursors cursors;
  if(!cursors.init()) {
    std::cerr << "SDL_CreateSystemCursor failed: " << SDL_GetError() << "\n";
  }
  auto updateCursor = [&](int width, int height) {
    cursors.apply(classifyCursor(text, ui, width, height));
  };

  auto autosaveWaitMs = [&]() -> int {
    if(!ui.state.hasLibrary() || !ui.editor.dirty() || ui.state.selection().noteId.empty()) return -1;
    const Uint64 now = SDL_GetTicks();
    const Uint64 next = std::max(ui.lastEdit + 1201, ui.lastAutosaveAttempt + 1001);
    if(now >= next) return 0;
    return std::clamp(static_cast<int>(next - now), 1, 1200);
  };

  // Everything the loop has to be awake for. Autosave used to be the only one,
  // so it was also the only thing the wait knew about.
  auto deadlines = [&]() -> FrameDeadlines {
    FrameDeadlines out;
    out.autosaveMs = autosaveWaitMs();
    // `IdleHint::Blinking` and `caretBlinkMs` were written for this and had no
    // caller: every caret was drawn solid, so nothing ever needed waking.
    out.caretBlinkMs = settleCaret(ui);
    // A drag past the edge of a list has to keep scrolling while the pointer is
    // perfectly still, which produces no events at all.
    out.hint = ui.textSelect.active || ui.sidebar.drag.active() || ui.blockDrag.active
                 ? IdleHint::Busy
               : out.caretBlinkMs >= 0 ? IdleHint::Blinking
                                       : IdleHint::Idle;
    return out;
  };

  applyWindowOptions(ui, options);

  if(!options.screenshotPath.empty()) {
    // An unmapped window reads back blank, so a capture maps it before drawing.
    SDL_ShowWindow(window);
    const int code = captureFrame(renderer, text, images, ui, options);
    cursors.destroy();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
  }

  revealWindow(window, [&](int width, int height) { drawApp(renderer, text, images, ui, width, height); });

  bool running = true;
  // `revealWindow` already painted what the loop's first pass would have.
  bool needsDraw = false;
  while(running) {
    SDL_Event event;
    const WaitDecision wait = chooseWait(deadlines());
    const bool hasEvent = wait.mode == WaitMode::Block
      ? SDL_WaitEvent(&event)
      : SDL_WaitEventTimeout(&event, wait.timeoutMs);
    perf::addCounter(perf::CounterId::FrameEventWakes);
    if(hasEvent) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      int drained = 0;
      do {
        ++drained;
        needsDraw = true;
      if(event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if(event.type == SDL_EVENT_TEXT_INPUT) {
        perf::addCounter(perf::CounterId::InputTextEvents);
        handleText(ui, event.text.text);
      } else if(event.type == SDL_EVENT_KEY_DOWN) {
        perf::addCounter(perf::CounterId::InputKeyEvents);
        // An open menu owns the keyboard first: arrows walk it, Enter chooses,
        // Escape shuts it. Anything else closes it and falls through, so typing
        // with a menu accidentally open does not silently go nowhere.
        //
        // Here rather than inside handleKey because the walk needs the bar's
        // geometry and the face it was measured in, and handleKey has neither
        // -- threading both through it would have every other branch carry them
        // for the one that uses them.
        bool menuTook = false;
        if(ui.chrome.openMenu != ui::MenuId::None) {
          const ShellLayout layout = shellLayout(ui, width, height);
          const MenuBarKey menu = handleMenuBarKey(
            text, ui, layout.menuBar,
            {0, 0, static_cast<float>(width), static_cast<float>(height)}, event.key.key);
          if(menu.action) {
            if(const auto* spec = ui::findAction(*menu.action)) {
              performCommand(ui, std::string(spec->name));
            }
          }
          menuTook = menu.handled;
        }
        if(!menuTook) handleKey(ui, event.key.key, event.key.scancode, event.key.mod);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        ui.pointer.x = event.button.x;
        ui.pointer.y = event.button.y;
        handleMouse(text, ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        ui.pointer.x = event.button.x;
        ui.pointer.y = event.button.y;
        handleMouseUp(ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_MOTION) {
        ui.pointer.x = event.motion.x;
        ui.pointer.y = event.motion.y;
        handleMouseMotion(text, ui, event.motion.x, event.motion.y, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL) {
        routeWheel(text, ui, event.wheel.y, width, height);
      } else if(event.type == SDL_EVENT_DROP_FILE) {
        if(event.drop.data) attachPathToEditor(ui, event.drop.data);
      } else if(event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        applyDisplayScale();
      } else if(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        rescanLibraryAfterExternalChange(ui);
      }
        // A held key or a fast trackpad refills the queue as fast as it
        // empties, and draining it whole starves the paint: the window stops
        // updating while input is still arriving. Stop at the budget and let
        // the frame go out; the rest is still queued.
        if(shouldYieldEventDrain(drained, needsDraw)) break;
      } while(SDL_PollEvent(&event));
    }
    if(applyPendingWindowAction(window, ui, running)) needsDraw = true;
    if(applyWatchedChanges(ui)) needsDraw = true;
    // The caret's blink, which is the one change with no event behind it.
    if(caretPhaseChanged(ui)) needsDraw = true;

    const Uint64 now = SDL_GetTicks();
    if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty() &&
       now - ui.lastEdit > 1200 && now - ui.lastAutosaveAttempt > 1000) {
      ui.lastAutosaveAttempt = now;
      (void)saveCurrent(ui, true);
      needsDraw = true;
    }
    if(needsDraw) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      drawApp(renderer, text, images, ui, width, height);
      needsDraw = false;
    } else {
      perf::addCounter(perf::CounterId::FrameRepaintsSkipped);
    }
  }

  if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty()) (void)saveCurrent(ui, true);
  persistLibraryState(ui);
  SDL_StopTextInput(window);
  cursors.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}
