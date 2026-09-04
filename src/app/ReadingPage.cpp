#include "app/ReadingPage.h"

#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "app/PageImages.h"
#include "app/PageView.h"
#include "app/WikiLinks.h"
#include "core/perf/Perf.h"
#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;
using ui::theme;

namespace {

// Installed once, for the life of the process, for the same reason the live
// page's are: each closure captures more than a `std::function`'s inline buffer
// holds, and rebuilding five of them per frame is five allocations and five
// frees on a frame that draws several hundred runs.
void wireReadingPage(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images,
                     UiRuntime& ui) {
  PageViewHooks hooks;
  hooks.measureComplex = [&text, &ui](const doc::SourceBlock& block, float width) {
    return measureComplexBlock(text, ui, block, width);
  };
  hooks.drawComplex = [renderer, &text, &ui](const doc::SourceBlock& block, Rect area) {
    drawComplexBlock(renderer, text, ui, block, area);
  };
  hooks.wikiLinkResolves = [&ui](std::string_view target) {
    return wikiLinkResolves(ui, target);
  };
  wirePageImages(hooks, renderer, images, ui);
  ui.readingPage.setHooks(std::move(hooks));
  ui.readingPage.setReadOnly(true);
}

}

void drawReading(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
                 Rect rect) {
  if(!ui.readingPage.wired()) wireReadingPage(renderer, text, images, ui);
  ui.readingPage.setWikiLinkRevision(ui.wikiNotesRevision);
  // A texture that has finished loading changes the height of the block showing
  // it, so the cache's generation is an input to the layout like the buffer is.
  ui.readingPage.setImageRevision(pageImageRevision(images));
  // No fold predicate: reading a note shows all of it. `setRevisions`' second
  // stamp is therefore constant, which is what tells the layout the fold state
  // never moves here.
  ui.readingPage.setRevisions(ui.editor.revision() + 1ull, 1);
  const auto& change = ui.editor.lastChange();
  ui.readingPage.setEditedSpan(change.toRevision == 0
                                 ? doc::LayoutOptions::EditedSpan {}
                                 : doc::LayoutOptions::EditedSpan {change.fromRevision + 1ull,
                                                                   change.toRevision + 1ull,
                                                                   change.start, change.oldEnd,
                                                                   change.newEnd});
  ui.readingPage.setPointer(ui.mouseX, ui.mouseY);
  // Measured before the layout: the header is room the page reserves at the top
  // of its scrolling space rather than a banner the note passes under.
  ui.readingPage.setHeaderHeight(pageHeaderHeight(text, ui));
  ui.readingPage.layout(text, ui.editor.text(), 0, rect);

  ui.readingPage.draw(renderer, text, 0, PageSelection {}, ui.focus == FocusArea::Viewer,
                      ui.find.text());
  {
    // Clipped to the page, so the header scrolls off the top rather than
    // running up over the tab strip on its way out.
    const ui::ClipGuard clip(renderer, ui.readingPage.pageRect());
    const Rect header = ui.readingPage.headerRect();
    drawPageHeader(renderer, text, ui, header, header.y);
  }
  for(const auto& link : ui.readingPage.links()) ui.linkRegions.push_back({link.rect, link.target, link.wiki});
  if(ui.editor.text().empty()) {
    // Where the note's first block would have been: on the content column,
    // under the header. Drawn at the page's own origin it would land on the
    // title `drawPageHeader` has just written there, so an empty note showed
    // its own name across the message telling you it was empty.
    const Rect column = ui.readingPage.columnRect();
    ui::drawEmptyMessage(text, "Nothing to read yet", "This note has no text in it.", column.x,
                         column.y, column.w,
                         ui.state.workspace().paneMode() == ui::PaneMode::Split
                           ? "type on the left"
                           : ui::keysFor(ui::ActionId::PaneLive) + "  go back and write");
  }
}

}
