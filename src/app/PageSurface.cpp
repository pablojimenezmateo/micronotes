#include "app/PageSurface.h"

#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "app/PageImages.h"
#include "app/WikiLinks.h"

#include <string_view>
#include <utility>

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;

void wirePage(PageView& page, SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images,
              UiRuntime& ui) {
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
  // The same pictures in both panes, fitted the same way. A note that shows an
  // image in one pane and its alt text in the other is exactly the divergence
  // merging the two renderers was for.
  wirePageImages(hooks, renderer, images, ui);
  page.setHooks(std::move(hooks));
}

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

void settlePageLayout(UiRuntime& ui, PageView& page) {
  applyQueuedAnchorJump(ui);
  sweepComplexCache(ui, page.document().blocks(), ui.editor.text());
}

void publishPageChrome(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, PageView& page) {
  {
    const ui::ClipGuard clip(renderer, page.pageRect());
    const Rect header = page.headerRect();
    drawPageHeader(renderer, text, ui, header, header.y);
  }
  for(const auto& link : page.links()) ui.linkRegions.push_back({link.rect, link.target, link.wiki});
}

}
