#include "CoreAliases.h"
#include "app/Application.h"

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
#include "ui/Painter.h"
#include "ui/TextRenderer.h"
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
        const EventOutcome outcome = routeEvent(event, text, ui, cursors, width, height);
        if(outcome.quit) running = false;
        // The loop's own business, because it owns the renderer's scale and the
        // face cache that has to be dropped with it.
        if(outcome.displayScaleChanged) applyDisplayScale();
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
  SDL_StopTextInput(window);
  cursors.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}
