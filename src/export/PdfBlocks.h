#pragma once

#include "core/pdf/PdfContent.h"
#include "core/pdf/PdfDocument.h"
#include "doc/Layout.h"
#include "export/PdfFaces.h"
#include "export/PdfPictures.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace micronotes::exporting {

// Painting a laid-out note onto a page, which is the same job
// `app/PageViewPaint.cpp` does onto a window.
//
// Two painters over one layout, deliberately -- not two renderers. The
// document model, the block partition, the line breaking and the run styling
// are `doc::`'s and are computed once for whichever surface asks; what differs
// here is the ink and the fact that a page ends. Anything that has to *decide*
// something about a note belongs in `doc::`, and anything either painter has
// to know that the other does not is what these two files are.
//
// The one structural difference from the window is that a block does not fit
// on a page. Everything below takes a slice of one -- a half-open range of the
// block's own vertical space -- and paints the lines and pictures inside it,
// with the block's chrome clipped to the same range. A quote that runs over a
// page break gets its rule on both pages, and each is the right length.
struct BlockInk {
  microcore::pdf::PdfDocument* document = nullptr;
  const PdfFaces* faces = nullptr;
  // Null for a note exported with pictures turned off, which is what a build
  // with no image decoder in it amounts to.
  PdfPictures* pictures = nullptr;
  // Where the live rect of every external link painted through this ink is
  // appended, if anywhere.
  //
  // An annotation is a property of the page *object* and a run is painted into
  // the page's content *stream*, so the rect cannot be emitted where it is
  // known -- it has to be collected while painting and handed to `addPage`
  // with the bytes. That is what this is, and why it is here rather than a
  // `link` call on `PdfContent`. It was TD-42.
  //
  // The screen's counterpart is `ui::RunPaint::links`, which collects the same
  // rects for the same reason: nothing that draws a run can also own what a
  // click on it does.
  std::vector<microcore::pdf::PdfDocument::Annotation>* links = nullptr;
};

// A run of `>` lines is one quote or one callout, and the container is drawn
// over the whole run rather than once per line -- the same rule the screen
// follows, and for the same reason: per-line containers leave seams.
//
// Which run a block is in cannot be read off the block, because only the run's
// *first* line carries the `[!KIND]` that decides whether it is a callout at
// all. So the composer works the runs out once and tells each block what it is
// inside of.
//
// `height` is what makes the run seamless across a page break as well: every
// block but the last in a run paints its ground down to where the *next*
// block starts, gap included, so the pieces tile into the single shape the
// screen draws in one go.
struct QuoteGround {
  bool inRun = false;
  bool callout = false;
  std::string_view kind;
  float height = 0.0f;
};

// The vertical band of a block's own space this call is about, and where it
// lands on the page. `pageY` is the page coordinate of block-space `from`.
struct BlockSlice {
  float from = 0.0f;
  float to = 0.0f;
  float pageY = 0.0f;
  // Where the block's left edge sits on the page, and how wide the column is.
  float columnLeft = 0.0f;
  float columnWidth = 0.0f;
  QuoteGround ground;
};

void paintBlockSlice(microcore::pdf::PdfContent& content, const BlockInk& ink,
                     const doc::DocumentLayout& layout, std::size_t index,
                     const BlockSlice& slice);

// Where one laid-out block's text goes and what colour its body takes.
struct RunPaint {
  // Where the layout's own origin sits on the page.
  float x = 0.0f;
  float y = 0.0f;
  // The ink a `TextRole::Body` run takes. A quote's body sits back a step, a
  // ticked task is muted, a callout's head line is drawn in the kind's own
  // colour -- every one of which is a decision about the *block*, not about
  // the run, which is why one colour is enough to say it. Every other role
  // inks itself through `ui::inkFor`.
  SDL_Color bodyInk;
  // Lines outside this band, in the layout's own space, are skipped: a block
  // taller than a page is painted a page at a time.
  float from = 0.0f;
  float to = 0.0f;
};

// One laid-out block's lines and runs, painted.
//
// Shared by the note's own blocks and by the md4c ones in `PdfComplex`, which
// each had a copy of this loop -- and the copies had already diverged: the
// md4c one drew no code ground, no strikethrough and no link rule, because it
// was painting a string rather than a run.
void paintRuns(microcore::pdf::PdfContent& content, const BlockInk& ink,
               const doc::BlockLayout& layout, const RunPaint& paint);

// Where a line's baseline sits inside its line box.
//
// The screen draws a run from the *top* of its box and lets the rasterizer
// place the baseline; a PDF is told the baseline directly. Taking it from the
// face's own ascent rather than from a fraction of the line height is what
// keeps a 20pt heading and a 9pt caption sitting on the same relationship to
// their boxes -- a fixed fraction puts one of them visibly off.
float baselineIn(const microcore::pdf::PdfFont& font, float lineHeight, float size);

}
