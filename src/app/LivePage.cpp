#include "app/LivePage.h"

#include "app/Folds.h"
#include "app/PageSurface.h"
#include "app/PageView.h"
#include "core/perf/Perf.h"
#include "doc/BlockScan.h"
#include "ui/Metrics.h"
#include "ui/Theme.h"

#include <algorithm>
#include <optional>

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;
using ui::theme;

namespace {

// Installed once, for the life of the process. See `wirePage`; the fold
// predicate is the one piece of wiring the reading pane does not share, because
// reading a note shows all of it.
void wireLivePage(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images,
                  UiRuntime& ui) {
  wirePage(ui.livePage, renderer, text, images, ui);
  ui.livePage.setFolds(livePageFolds(ui));
}

// The live surface's own half of the frame contract: the four inputs a reading
// pane leaves at their defaults, plus the fold state.
PageFrame liveFrame(TextRenderer& text, ui::ImageCache& images, UiRuntime& ui) {
  PageFrame frame = pageFrameFor(text, images, ui);
  // Whether this note has anything collapsed is per-frame state, not a closure:
  // the layout skips resolving folds entirely when it is told there is no
  // predicate to ask, and that has to stay true for a note with no folds in it
  // without rebuilding the predicate to say so.
  const NoteFoldStamp folds = noteFoldStamp(ui.folds, ui.state.selection().noteId);
  frame.foldsActive = folds.anyFolded;
  frame.foldRevision = folds.stamp;
  frame.blockSelection = {ui.blockSelection.active, ui.blockSelection.anchor, ui.blockSelection.focus};
  frame.dropOffset = ui.blockDrag.active ? ui.blockDrag.dropOffset : std::nullopt;
  frame.selecting = ui.textSelect.active;
  // While the find bar is up, every selection on the page is one the search
  // made rather than one the reader did.
  frame.offerToolbar = !ui.find.open;
  frame.caretVisible = ui.caret.visible;
  return frame;
}

// A `Complex` block the caret has left is handed back to md4c, which needs a
// second layout because the block's height changes with it.
void releaseRawBlock(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const auto raw = ui.livePage.rawOffset();
  if(!raw) return;
  const auto& blocks = ui.livePage.document().blocks();
  const auto index = doc::blockIndexAt(blocks, std::min(*raw, ui.editor.text().size()));
  const auto& block = blocks[index];
  const auto cursor = ui.editor.cursor();
  if(cursor >= block.start && cursor < block.end()) return;
  ui.livePage.setRawOffset(std::nullopt);
  ui.livePage.layout(text, ui.editor.text(), cursor, rect);
}

}

void drawLive(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
              Rect rect) {
  if(!ui.livePage.wired()) wireLivePage(renderer, text, images, ui);
  ui.livePage.beginFrame(liveFrame(text, images, ui));
  ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), rect);
  // Before the settle rather than in the middle of it. Handing a block back to
  // md4c changes its height, so a queued anchor jump resolved first would be
  // scrolling to a position the second layout moves.
  releaseRawBlock(text, ui, rect);
  settlePageLayout(ui, ui.livePage);

  if(ui.revealEditorCursor) {
    ui.livePage.revealCaret(ui.editor.cursor());
    ui.revealEditorCursor = false;
  }
  PageSelection selection;
  if(ui.editor.hasSelection()) {
    selection.start = ui.editor.selectionStart();
    selection.end = ui.editor.selectionEnd();
  }
  ui.livePage.draw(renderer, text, ui.editor.cursor(), selection, ui.focus == FocusArea::Editor,
                   ui.find.matches, ui.find.activeIndex());
  publishPageChrome(renderer, text, ui, ui.livePage);

  if(ui.editor.text().empty()) {
    // On the content column rather than the page edge, so the prompt sits
    // exactly where the first character typed will appear.
    const Rect column = ui.livePage.columnRect();
    const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().body};
    text.draw("Write something. Press / for a block.", column.x, column.y, theme().textMuted, style);
  }
}

}
