#include "app/ReadingPage.h"

#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "app/PageImages.h"
#include "app/PageView.h"
#include "app/WikiLinks.h"
#include "core/perf/Perf.h"
#include "ui/Actions.h"
#include "ui/ClipGuard.h"
#include "ui/Metrics.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <string_view>
#include <utility>

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;

namespace {

// Installed once, for the life of the process, not per frame. Each closure
// captures more than a `std::function`'s inline buffer holds, so rebuilding
// them per frame was five heap allocations and five frees on a frame that draws
// several hundred runs. They read the current note and the current buffer *when
// they are called* rather than capturing either, which is what makes installing
// them once correct rather than merely cheaper -- and the renderer, the text
// renderer and the runtime are created in `run()` and outlive every frame,
// which is the lifetime these references need.
//
// What the page cannot answer itself: measure and draw a block the scanner does
// not model, resolve a wikilink, and fit a picture.
void wirePage(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui) {
  PageViewHooks hooks;
  hooks.measureComplex = [&text, &ui](const doc::SourceBlock& block, float width) {
    return measureComplexBlock(text, ui, block, width);
  };
  hooks.drawComplex = [renderer, &text, &ui](const doc::SourceBlock& block, Rect area) {
    drawComplexBlock(renderer, text, ui, block, area);
  };
  // Asked once per wikilink per layout, so the answer is memoised against the
  // library's refresh generation rather than re-listing every note per link.
  hooks.wikiLinkResolves = [&ui](std::string_view target) { return wikiLinkResolves(ui, target); };
  wirePageImages(hooks, renderer, images, ui);
  ui.readingPage.setHooks(std::move(hooks));
}

// Everything this frame's layout depends on, in one value. See `PageFrame` for
// why it is one value rather than a sequence of setters.
PageFrame pageFrameFor(TextRenderer& text, ui::ImageCache& images, UiRuntime& ui) {
  PageFrame frame;
  frame.sourceRevision = ui.editor.revision();
  frame.editedSpan = ui.editor.lastChange();
  frame.wikiLinkRevision = ui.wikiTargets.revision();
  // A texture that has finished loading changes the height of the block showing
  // it, so the cache's generation is an input to the layout like the buffer is.
  frame.imageRevision = pageImageRevision(images);
  // Measured before the layout, because the header is room the page has to
  // reserve at the top of its scrolling space rather than something drawn over
  // it afterwards.
  frame.headerHeight = pageHeaderHeight(text, ui);
  frame.pointerX = ui.pointer.x;
  frame.pointerY = ui.pointer.y;
  return frame;
}

// Between the layout and the paint, in this order.
//
// A queued cross-note anchor can only be resolved now, because the page is only
// now holding the note the link opened -- see `queueAnchorJump`. And the parse
// cache mirrors this page's `Complex` blocks, so this is the only place that
// knows which of them the note no longer contains; one comparison, on a frame
// that laid out nothing new.
void settlePageLayout(UiRuntime& ui) {
  applyQueuedAnchorJump(ui);
  sweepComplexCache(ui, ui.readingPage.document().blocks(), ui.editor.text());
}

// After the paint: the note's header, and the links the shell hit-tests.
//
// The header is clipped to the page so it scrolls off the top rather than
// running up over the tab strip on its way out. The links are filled by the
// paint, which is why this cannot be folded into `settlePageLayout`.
void publishPageChrome(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui) {
  {
    const ui::ClipGuard clip(renderer, ui.readingPage.pageRect());
    const Rect header = ui.readingPage.headerRect();
    drawPageHeader(text, ui, header, header.y);
  }
  for(const auto& link : ui.readingPage.links()) {
    ui.linkRegions.push_back({link.rect, link.target, link.wiki});
  }
}

}

void drawReading(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
                 Rect rect) {
  if(!ui.readingPage.wired()) wirePage(renderer, text, images, ui);
  ui.readingPage.beginFrame(pageFrameFor(text, images, ui));
  ui.readingPage.layout(text, ui.editor.text(), rect);
  settlePageLayout(ui);

  // The buffer's selection, which is what a drag in this pane makes: the pane
  // is read-only, not untouchable, and a reader selecting a paragraph to copy
  // has to be able to see what they have.
  PageSelection selection;
  if(ui.editor.hasSelection()) {
    selection.start = ui.editor.selectionStart();
    selection.end = ui.editor.selectionEnd();
  }
  if(ui.revealViewerSelection) {
    // The pane has no caret, so nothing else ever scrolls it to a position in
    // the buffer. Stepping through find matches does, and without this the
    // match the reader was walked to could be a page off screen.
    ui.readingPage.revealOffset(selection.start);
    ui.revealViewerSelection = false;
  }
  ui.readingPage.draw(renderer, text, selection, ui.focus == FocusArea::Viewer,
                      ui.find.matches, ui.find.activeIndex());
  publishPageChrome(renderer, text, ui);

  if(ui.editor.text().empty()) {
    // Where the note's first block would have been: on the content column,
    // under the header. Drawn at the page's own origin it would land on the
    // title `drawPageHeader` has just written there, so an empty note showed
    // its own name across the message telling you it was empty.
    const Rect column = ui.readingPage.columnRect();
    ui::drawEmptyMessage(text, "Nothing to read yet", "This note has no text in it.", column.x,
                         column.y, column.w,
                         ui.paneMode() == ui::PaneMode::Split
                           ? "type on the left"
                           : ui::keysFor(ui::ActionId::PaneSplit) + "  write beside it");
  }
}

}
