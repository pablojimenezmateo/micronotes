#include "app/ReadingPage.h"

#include "app/PageSurface.h"
#include "app/PageView.h"
#include "core/perf/Perf.h"
#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/Theme.h"

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

  ui.readingPage.draw(renderer, text, 0, PageSelection {}, ui.focus == FocusArea::Viewer,
                      ui.fields.find.text());
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
