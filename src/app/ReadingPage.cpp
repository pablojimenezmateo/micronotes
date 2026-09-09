#include "app/ReadingPage.h"

#include "app/PageSurface.h"
#include "app/PageView.h"
#include "core/perf/Perf.h"
#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace micronotes::app {

using ui::Rect;
using ui::TextRenderer;

void drawReading(SDL_Renderer* renderer, TextRenderer& text, ui::ImageCache& images, UiRuntime& ui,
                 Rect rect) {
  if(!ui.readingPage.wired()) {
    wirePage(ui.readingPage, renderer, text, images, ui);
    ui.readingPage.setReadOnly(true);
  }
  // No fold state and none of the live surface's four: `PageFrame`'s defaults
  // are what "reading a note shows all of it, with nothing selected and no
  // caret" is spelled as. The reading pane used to say each of those by
  // omission, which is the same thing until somebody adds a sixth input.
  ui.readingPage.beginFrame(pageFrameFor(text, images, ui));
  ui.readingPage.layout(text, ui.editor.text(), 0, rect);
  settlePageLayout(ui, ui.readingPage);

  // The buffer's selection, which is what a drag in this pane makes: the pane
  // is read-only, not untouchable, and a reader selecting a paragraph to copy
  // has to be able to see what they have.
  PageSelection selection;
  if(ui.editor.hasSelection()) {
    selection.start = ui.editor.selectionStart();
    selection.end = ui.editor.selectionEnd();
  }
  if(ui.revealViewerSelection) {
    // The reading pane has no caret, so nothing else ever scrolls it to a
    // position in the buffer. Stepping through find matches does, and without
    // this the match the reader was walked to could be a page off screen while
    // the live pane beside it had already scrolled.
    ui.readingPage.revealCaret(selection.start);
    ui.revealViewerSelection = false;
  }
  ui.readingPage.draw(renderer, text, 0, selection, ui.focus == FocusArea::Viewer,
                      ui.find.matches, ui.find.activeIndex());
  publishPageChrome(renderer, text, ui, ui.readingPage);

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
                           : ui::keysFor(ui::ActionId::PaneLive) + "  go back and write");
  }
}

}
