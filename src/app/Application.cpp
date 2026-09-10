#include "CoreAliases.h"
#include "app/Application.h"

#include "app/AppWindow.h"
#include "app/Autosave.h"
#include "app/CaretPolicy.h"
#include "app/Chrome.h"
#include "app/Clipboard.h"
#include "app/Commands.h"
#include "app/Cursor.h"
#include "app/EventRouter.h"
#include "app/Frame.h"
#include "app/FramePolicy.h"
#include "app/FrameTrace.h"
#include "app/InputDebug.h"
#include "app/KeyRouter.h"
#include "app/Layout.h"
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
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"
#include "ui/Fonts.h"
#include "ui/Menus.h"
#include "ui/Metrics.h"
#include "ui/ShellLayout.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

#include <algorithm>
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

  if(options.theme) ui::setThemeMode(*options.theme);

  // Declared before the two texture caches below, which is what puts them
  // ahead of it in destruction order: `SDL_Renderer` owns every texture made
  // against it, so a cache outliving the renderer calls `SDL_DestroyTexture`
  // on memory SDL has already freed. `app/AppWindow.h` has the rest of why
  // this is a type and not five locals.
  AppWindow app;
  if(!app.open(options.windowWidth, options.windowHeight)) return 1;
  SDL_Window* window = app.window();
  SDL_Renderer* renderer = app.renderer();

  // Held for the lifetime of the window: SDL keeps the pointer and calls back
  // into it on every pointer press near the frame.
  HitTestContext hitTestContext;
  installWindowHitTest(window, renderer, ui, hitTestContext);

  installWatcherWake(ui);
  TextRenderer text(renderer);
  app.applyDisplayScale(text, options.scale);
  if(inputDebugEnabled()) {
    std::cerr << "fonts source=\"" << text.fonts().sourceDescription() << "\""
              << " ready=" << text.fonts().ready() << "\n";
  }
  ImageCache images(renderer);
  applyWindowOptions(ui, options);

  if(!options.screenshotPath.empty()) {
    // An unmapped window reads back blank, so a capture maps it before drawing.
    SDL_ShowWindow(window);
    return captureFrame(renderer, text, images, ui, options);
  }

  revealWindow(window, [&](int width, int height) { drawApp(renderer, text, images, ui, width, height); });

  bool running = true;
  // `revealWindow` already painted what the loop's first pass would have.
  bool needsDraw = false;
  while(running) {
    SDL_Event event;
    const WaitDecision wait = chooseWait(frameDeadlines(ui, SDL_GetTicks()));
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
        // Every event repaints, motion included, and that is not laziness.
        //
        // On Wayland a cursor shape rides a hardware overlay plane the
        // compositor re-latches on a surface commit, not on the
        // `wl_pointer.set_cursor` request itself -- so a motion that changes
        // only the cursor and repaints nothing leaves the *old* shape on
        // screen. `../microide` shipped that bug for its whole life and its
        // `dev-docs/platform/wayland-stale-cursor.md` is the write-up: the tell
        // was that it went away while screen-recording, because a recorder
        // forces continuous recomposition.
        //
        // So if this is ever narrowed to "repaint when something visible
        // changed", the cursor has to ask for a present of its own on a real
        // shape change. Hovering a link changes no pixels at all.
        needsDraw = true;
        const EventOutcome outcome = routeEvent(event, text, ui, app.cursors(), width, height);
        if(outcome.quit) running = false;
        // Handed straight back to the window, which owns the renderer's scale
        // and the face cache that has to be dropped with it.
        if(outcome.displayScaleChanged) app.applyDisplayScale(text, options.scale);
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
    if(autosaveDue(ui, now)) {
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

  if(autosavePending(ui)) (void)saveCurrent(ui, true);
  persistLibraryState(ui);
  return 0;
}

}
