#pragma once

#include "core/markdown/MarkdownParser.h"
#include "core/pdf/PdfContent.h"
#include "core/pdf/PdfDocument.h"
#include "doc/RenderLayout.h"
#include "export/PdfBlocks.h"
#include "export/PdfFaces.h"
#include "ui/DocStyle.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace micronotes::exporting {

// Where one complex block's slices land on the page.
//
// One value rather than seven positional arguments, three of which are floats
// that mean different things: `paint(content, source, width, x, y, from, to)`
// is a call nobody can read and any two of whose arguments can be swapped
// without a compiler noticing.
struct ComplexSlice {
  // The block's own bytes, which its layout is memoised under.
  std::string_view source;
  // The column the block was laid out to, and where its left edge sits.
  float width = 0.0f;
  float x = 0.0f;
  // Where the top of the first slice goes on the page.
  float y = 0.0f;
  // Half-open range of `doc::RenderedSlice`s -- a table's rows are its atoms,
  // so a table longer than a page becomes several pages of table rather than
  // one page and a hole.
  std::size_t from = 0;
  std::size_t to = 0;
  // Where the live rect of every external link in these slices is appended.
  // See `BlockInk::links`: a link inside a table cell is a link, and it was
  // dead on the page for as long as nothing collected it.
  std::vector<microcore::pdf::PdfDocument::Annotation>* links = nullptr;
};

// The blocks `doc::BlockScan` deliberately does not model -- tables, raw HTML,
// footnote definitions -- laid out for the page.
//
// The layout is `doc::layoutRenderedBlock`, which is the same shaping the
// reading pane uses, so the two agree about what a table is down to where its
// lines break. This class is the page's *painter* over it, plus the memo the
// composer needs because it measures each block and then draws it.
//
// It used to be `PdfTables`, and it was three things at once: a second md4c
// renderer, a plain-text line breaker, and a table painter. What that cost is
// worth recording, because it is the shape of the debt this replaced.
//
//   * A cell was set with `markdown::plainText`, so bold, italics, code spans
//     and links inside one were flattened to their words (TD-39).
//   * Only the *first* table in a block was drawn, and every other block in it
//     -- a paragraph above the table, a second table below it -- was dropped
//     from the export while the screen drew all of them.
//   * A lone footnote definition was parsed without the synthetic reference
//     md4c needs to keep it, so the same note showed a footnote on screen and
//     grey monospace source on the page.
//   * A block with no table in it took the raw-source fallback whatever it
//     was, and that fallback does not wrap -- so a long line of raw HTML was
//     drawn straight off the right margin. It goes through the line breaker
//     now, and the fallback is what it says it is: the last resort for a block
//     md4c renders to nothing at all.
//   * A `[[target]]` inside a table was never asked about, so a wikilink in a
//     cell exported as a resolved link while the same wikilink in the
//     paragraph above it exported as pending.
//
// All three came from the same cause: a renderer written twice. There is one
// now, and it is below both surfaces.
class PdfComplex {
public:
  PdfComplex(microcore::pdf::PdfDocument& document, const PdfFaces& faces)
    : document_(&document), faces_(&faces) {}

  // Whether a `[[target]]` names a note that exists, which decides whether a
  // wikilink is drawn as a link or as an offer.
  //
  // Set for the same reason the note's own blocks are given it: without it a
  // `[[Missing]]` in a table exported as a resolved link while the same
  // `[[Missing]]` in the paragraph above it exported as pending -- one note,
  // two answers to one question, because only one of the two paths was asked.
  void resolveWikiLinksWith(std::function<bool(std::string_view)> resolves);

  // The block, parsed and laid out at `width`. Memoised on the block's own
  // source, which is what the reading pane keys its cache on too -- the
  // composer asks for a height once per block and the paint asks again.
  const doc::RenderedBlock& layoutOf(std::string_view source, float width);

  // Draws `slice`'s range of slices, the first of them with its top at its
  // `y`.
  void paint(microcore::pdf::PdfContent& content, const ComplexSlice& slice);

  // What the page's own type, paddings and measure are. Public because the
  // composer reads the repeated-header height off the same context.
  doc::RenderContext context() const;

private:
  doc::RenderedBlock& entryFor(std::string_view source, float width);
  // One row of a table, ground and rules and cells. Its own function because
  // the header row is drawn twice on a table that runs over a page break.
  void paintTableRow(microcore::pdf::PdfContent& content, const BlockInk& ink,
                     const doc::TableLayout& table, std::size_t rowIndex, float x, float y);

  microcore::pdf::PdfDocument* document_ = nullptr;
  const PdfFaces* faces_ = nullptr;
  microcore::markdown::MarkdownParser parser_;
  std::function<bool(std::string_view)> wikiResolves_;
  std::unordered_map<std::string, doc::RenderedBlock> cache_;
};

}
