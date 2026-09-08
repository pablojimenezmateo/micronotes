#include "app/Frame.h"

#include "app/Breadcrumb.h"
#include "app/Chrome.h"
#include "app/FrameTrace.h"
#include "app/LivePage.h"
#include "app/MenuBar.h"
#include "app/RawPane.h"
#include "app/ReadingPage.h"
#include "app/RightPanel.h"
#include "app/Screenshot.h"
#include "app/Shell.h"
#include "app/Sidebar.h"
#include "app/TabStrip.h"
#include "core/perf/Perf.h"
#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/ShellLayout.h"
#include "ui/Theme.h"
#include "ui/Tooltip.h"

namespace micronotes::app {
namespace {

using micronotes::ui::ImageCache;
using micronotes::ui::drawEmptyMessage;
using micronotes::ui::drawTooltip;
using micronotes::ui::fill;
using micronotes::ui::theme;

}

void drawApp(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, int width, int height) {
  ScopedFrame frame;
  SDL_SetRenderDrawColor(renderer, theme().windowBackground.r, theme().windowBackground.g, theme().windowBackground.b, theme().windowBackground.a);
  SDL_RenderClear(renderer);

  const ShellLayout layout = shellLayout(ui, width, height);
  // Before anything draws a caret, and recorded so the loop can tell when the
  // blink has flipped underneath it. See `settleCaret`, `caretPhaseChanged`.
  (void)settleCaret(ui);
  ui.caret.painted = ui.caret.visible;
  ui.linkRegions.clear();
  // Refilled by the strip if it is drawn at all; a hidden strip answers no
  // clicks because there is nothing recorded to hit.
  ui.tabStrip.clear();
  // Cleared here and set by whichever surface the pointer turns out to be over,
  // so a frame can never end up with two tooltips resolved.
  ui.pointer.tooltip = {};

  // First, above everything: it carries the menus and the window controls, and
  // the popup drawn at the end of the frame hangs off it.
  ui.chrome.menuBarRect = layout.menuBar;
  {
    const perf::ScopeTimer timer("shell.menu_bar");
    drawMenuBar(renderer, text, ui, layout.menuBar);
  }
  // A hidden panel is zero wide, and its rule would land on the edge of
  // whatever took its place.
  if(!ui::empty(layout.sidebar)) {
    const perf::ScopeTimer timer("shell.sidebar");
    drawSidebar(renderer, text, ui, layout.sidebar);
    fill(renderer, {layout.sidebar.x + layout.sidebar.w, layout.sidebar.y, 1, layout.sidebar.h}, theme().border);
  }
  if(!ui::empty(layout.tabs)) {
    const perf::ScopeTimer timer("shell.tab_strip");
    drawTabStrip(renderer, text, ui, layout.tabs);
  }
  {
    const perf::ScopeTimer timer("shell.breadcrumb");
    drawBreadcrumb(renderer, text, ui, layout.breadcrumb);
  }
  if(!ui.state.hasLibrary()) {
    fill(renderer, layout.content, theme().editorBackground);
    // The one screen someone can arrive at knowing nothing, so it says what
    // the app is for before it says which key to press.
    drawEmptyMessage(text, "Open a folder of notes",
                     "micronotes reads and writes plain Markdown files in one local folder. Nothing leaves your disk.",
                     layout.content.x + 18.0f, layout.content.y + 40.0f, layout.content.w - 36.0f,
                     ui::keysFor(ui::ActionId::Settings) + "  Settings          or start with  --library <path>");
  } else if(ui.state.selection().noteId.empty()) {
    fill(renderer, layout.content, theme().editorBackground);
    drawEmptyMessage(text, "Nothing open", "Pick a note from the sidebar, or start a new one.",
                     layout.content.x + 18.0f, layout.content.y + 40.0f, layout.content.w - 36.0f,
                     ui::keysFor(ui::ActionId::GoToNote) + "  go to note          " + ui::keysFor(ui::ActionId::NewNote) +
                     "  new note          " + ui::keysFor(ui::ActionId::Shortcuts) + "  every shortcut");
  } else {
    const perf::ScopeTimer timer("shell.content");
    const Rect content = layout.content;
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      drawLive(renderer, text, images, ui, content);
    } else if(ui.state.workspace().paneMode() == ui::PaneMode::Editor) {
      drawEditor(renderer, text, ui, content);
    } else if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) {
      drawReading(renderer, text, images, ui, content);
    } else {
      const float split = content.w / 2.0f;
      drawEditor(renderer, text, ui, {content.x, content.y, split, content.h});
      fill(renderer, {content.x + split, content.y, 1, content.h}, theme().border);
      drawReading(renderer, text, images, ui, {content.x + split, content.y, content.w - split, content.h});
    }
  }
  // After the content: its outline borrows the partition the live page splices.
  if(!ui::empty(layout.rightPanel)) {
    const perf::ScopeTimer timer("shell.right_panel");
    drawRightPanel(renderer, text, ui, layout.rightPanel);
  }
  {
    const perf::ScopeTimer timer("shell.status");
    fill(renderer, layout.status, theme().chromeBackground);
    fill(renderer, {layout.status.x, layout.status.y, layout.status.w, 1}, theme().border);
    drawStatus(renderer, text, ui, layout.status);
  }
  // An open overlay is a conversation; a tooltip about what is behind it would
  // be answering a question nobody is asking any more.
  if(ui.overlays.active()) ui.pointer.tooltip = {};
  {
    // After every panel, so it lands on top of whichever one it hangs over.
    const perf::ScopeTimer timer("shell.menu");
    drawOpenMenu(renderer, text, ui, {0, 0, static_cast<float>(width), static_cast<float>(height)});
  }
  {
    const perf::ScopeTimer timer("shell.overlays");
    ui.overlays.setCaretVisible(ui.caret.visible);
    ui.overlays.draw(renderer, text, width, height);
    // Last, so nothing paints over it.
    drawTooltip(renderer, text, ui.pointer.tooltip, {0, 0, static_cast<float>(width), static_cast<float>(height)});
  }
  // The frame's work ends here. With vsync on, the present below blocks until
  // the display is ready, so charging that wait to the frame reports the refresh
  // interval as if it were the app's cost.
  frame.markWorkDone();
  {
    const perf::ScopeTimer timer("shell.present");
    SDL_RenderPresent(renderer);
  }
}

int captureFrame(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, const ApplicationOptions& options) {
  return captureWindowToFile(renderer, options.screenshotPath, options.windowWidth, options.windowHeight,
                             [&](int width, int height) { drawApp(renderer, text, images, ui, width, height); });
}

}
