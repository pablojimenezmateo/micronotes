#include "app/LivePage.h"

#include "app/Folds.h"
#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "app/PageImages.h"
#include "app/PageView.h"
#include "app/WikiLinks.h"
#include "core/perf/Perf.h"
#include "doc/BlockScan.h"
#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/Theme.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;
using ui::theme;

namespace {

// Installed once, for the life of the process.
//
// All five closures used to be built and moved into the page on every frame,
// and each captures more than a `std::function`'s inline buffer holds -- so an
// idle frame was five heap allocations and five frees for closures whose
// captures (the renderer, the text renderer, the runtime) never change. They
// read the current note and the current buffer *when they are called* instead
// of capturing either, which is what makes installing them once correct rather
// than merely cheaper.
//
// The renderer, the text renderer and the runtime are created once in `run()`
// and outlive every frame, which is the lifetime these references need.
void wireLivePage(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images,
                  UiRuntime& ui) {
  PageViewHooks hooks;
  hooks.measureComplex = [&text, &ui](const doc::SourceBlock& block, float width) {
    return measureComplexBlock(text, ui, block, width);
  };
  // Asked once per wikilink per layout, so the answer is memoised against the
  // library's refresh generation rather than re-listing every note per link.
  hooks.wikiLinkResolves = [&ui](std::string_view target) {
    return wikiLinkResolves(ui, target);
  };
  hooks.drawComplex = [renderer, &text, &ui](const doc::SourceBlock& block, Rect area) {
    drawComplexBlock(renderer, text, ui, block, area);
  };
  // The same pictures the reading pane draws, fitted the same way. A note that
  // shows an image in one pane and its alt text in the other is exactly the
  // divergence merging the two renderers was for.
  wirePageImages(hooks, renderer, images, ui);
  ui.livePage.setHooks(std::move(hooks));
  ui.livePage.setFolds(livePageFolds(ui));
}

}

void drawLive(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
              Rect rect) {
  if(!ui.livePage.wired()) wireLivePage(renderer, text, images, ui);
  ui.livePage.setWikiLinkRevision(ui.wikiNotesRevision);
  ui.livePage.setImageRevision(pageImageRevision(images));
  // Whether this note has anything collapsed is per-frame state, not a
  // closure: the layout skips resolving folds entirely when it is told there is
  // no predicate to ask, and that has to stay true for a note with no folds in
  // it without rebuilding the predicate to say so.
  const NoteFoldStamp folds = noteFoldStamp(ui.folds, ui.state.selection().noteId);
  ui.livePage.setFoldsActive(folds.anyFolded);
  // The source stamp is the editor's revision; +1 because zero means "cannot
  // say" to the layout's reuse check.
  ui.livePage.setRevisions(ui.editor.revision() + 1ull, folds.stamp);
  // And where that revision differs from the one before it, in the same +1
  // space, so the layout can bound the comparison it would otherwise make over
  // the whole note to find the edit. It checks the stamps against the buffer it
  // holds, so a claim from a frame that handled two keystrokes is discarded
  // there rather than believed here.
  const auto& change = ui.editor.lastChange();
  ui.livePage.setEditedSpan(change.toRevision == 0
                              ? doc::LayoutOptions::EditedSpan {}
                              : doc::LayoutOptions::EditedSpan {change.fromRevision + 1ull,
                                                               change.toRevision + 1ull,
                                                               change.start, change.oldEnd,
                                                               change.newEnd});
  ui.livePage.setPointer(ui.mouseX, ui.mouseY);
  ui.livePage.setBlockSelection({ui.blockSelectActive, ui.blockSelectAnchor, ui.blockSelectFocus});
  ui.livePage.setDropOffset(ui.draggingBlock ? ui.blockDropOffset : std::nullopt);
  ui.livePage.setSelecting(ui.selectingEditorText);
  // Measured before the layout, because the header is room the page has to
  // reserve at the top of its scrolling space rather than something drawn over
  // it afterwards.
  ui.livePage.setHeaderHeight(pageHeaderHeight(text, ui));
  ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), rect);

  // Leaving a block that was dropped to raw text hands it back to md4c.
  if(const auto raw = ui.livePage.rawOffset()) {
    const auto& blocks = ui.livePage.document().blocks();
    const auto index = doc::blockIndexAt(blocks, std::min(*raw, ui.editor.text().size()));
    const auto& block = blocks[index];
    const auto cursor = ui.editor.cursor();
    if(cursor < block.start || cursor >= block.end()) {
      ui.livePage.setRawOffset(std::nullopt);
      ui.livePage.layout(text, ui.editor.text(), cursor, rect);
    }
  }

  // What the note no longer contains, dropped: the parse cache mirrors this
  // page's `Complex` blocks, and this is the only place that knows which they
  // are. One comparison on a frame that laid out nothing new.
  sweepComplexCache(ui, ui.livePage.document().blocks(), ui.editor.text());

  if(ui.revealEditorCursor) {
    ui.livePage.revealCaret(ui.editor.cursor());
    ui.revealEditorCursor = false;
  }
  PageSelection selection;
  if(ui.editor.hasSelection()) {
    selection.start = ui.editor.selectionStart();
    selection.end = ui.editor.selectionEnd();
  }
  ui.livePage.draw(renderer, text, ui.editor.cursor(), selection, ui.focus == FocusArea::Editor, ui.find.text());
  {
    // Clipped to the page, so the header scrolls off the top rather than
    // running up over the tab strip on its way out.
    const ui::ClipGuard clip(renderer, ui.livePage.pageRect());
    const Rect header = ui.livePage.headerRect();
    drawPageHeader(renderer, text, ui, header, header.y);
  }
  for(const auto& link : ui.livePage.links()) ui.linkRegions.push_back({link.rect, link.target, link.wiki});
  if(ui.editor.text().empty()) {
    // On the content column rather than the page edge, so the prompt sits
    // exactly where the first character typed will appear.
    const Rect column = ui.livePage.columnRect();
    const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().body};
    text.draw("Write something. Press / for a block.", column.x, column.y, theme().dim, style);
  }
}

}
