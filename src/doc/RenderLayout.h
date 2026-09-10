#pragma once

#include "CoreAliases.h"

#include "core/markdown/MarkdownParser.h"
#include "core/markdown/RenderModel.h"
#include "doc/Layout.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Laying out md4c's render model -- a table, a footnote definition, raw HTML,
// and every inline construct inside one -- through the same tokenizer and the
// same line breaker the note page's own blocks go through.
//
// **Why this exists.** The live scanner in `doc/BlockScan.h` does not model
// every block a Markdown file can hold, so those blocks are parsed on their own
// by md4c and rendered from `markdown::Block`. There were two renderers for
// that model and neither shared a line with the other: `app/InlineText.cpp` for
// the screen and `export/PdfTables.cpp` for the page. The screen's one carried
// a *second* line breaker -- a word tokenizer and a wrap loop of its own,
// beside the tested one in `doc/Flow.h` -- and the exporter's, unable to reach
// it from a lower layer, set every table cell as `markdown::plainText` and lost
// the bold, the italics, the code spans and the links inside it (that was
// TD-39).
//
// So the shaping is here, in the layer both of them can see, and it produces
// the same `doc::BlockLayout` the note page's own blocks produce. What is left
// above is two painters over one layout, which is the rule the rest of the
// export path already follows.
//
// **The offsets are local.** A `TextRun` in one of these carries
// `srcStart`/`srcEnd` into the flattened `InlineLayout::text`, *not* into the
// note's buffer: md4c hands back no source offsets, so there is nothing to
// address the buffer with. That is why one of these is never handed to
// `DocumentLayout`'s caret and selection queries, and why a table on the page
// is still one addressable position -- see TD-41.
namespace micronotes::doc {

// The paddings a table is measured and drawn with. One value, because the
// measure and the paint need every one of them and a table measured with one
// inset and drawn with another has rules that miss its own text. Screen
// numbers by default; the exporter scales them into points.
struct TableGeometry {
  float padX = 7.0f;
  float padY = 8.0f;
  // Below a couple of characters a cell shows nothing at all and a row of
  // slivers is not a table. The floor yields to the page rather than the other
  // way round: a table with more columns than fit reflows into slivers rather
  // than running off the right edge, which is the choice both surfaces make.
  float minCellWidth = 24.0f;
  // Clear space under the last row, so the next block does not sit on the rule.
  float tailGap = 10.0f;
};

// The vertical rhythm a rendered block is set to. Screen numbers by default;
// the exporter scales them into points. One value because the measure and the
// paint have to agree about every one of them -- a block measured with one gap
// and drawn with another leaves the next block sitting on it.
struct RenderSpacing {
  float padTop = 10.0f;
  float padBottom = 10.0f;
  // Between one run of inlines and the next.
  float blockGap = 6.0f;
  // Under a table, on top of the table's own tail gap: a table is a box and
  // wants more air than a paragraph does.
  float tableGap = 12.0f;
};

// Everything laying md4c's model out needs that is not the model itself.
struct RenderContext {
  // Only the text half: there is no document under a table cell, so nothing
  // here could answer `measureComplex` or `measureImage`.
  TextMetrics metrics;
  TypeMetrics type;
  TableGeometry table;
  RenderSpacing spacing;
  // Whether a `[[target]]` names a note that exists. Unset means "assume it
  // does", the same contract `LayoutOptions::wikiLinkResolves` has, because it
  // answers the same question for the other surface.
  std::function<bool(std::string_view)> wikiLinkResolves;
};

// One run of inlines, tokenized, broken to a width and placed.
//
// `text` has to outlive the layout and be moved with it: the runs' offsets
// address it. It is the block's visible characters with the markup gone --
// `**bold**` is the four bytes `bold` -- so it is not the source and cannot be
// used as it.
struct InlineLayout {
  BlockLayout layout;
  std::string text;
  // The widest line's right edge, which is what a centred or right-aligned
  // table cell is placed by. Not the width it was broken to: a two-word cell
  // in a wide column has to be centred on the two words.
  float width = 0.0f;

  float height() const {
    return layout.height;
  }
  std::size_t lineCount() const {
    return layout.lines.empty() ? 1 : layout.lines.size();
  }
};

// One laid-out table. Every cell is an `InlineLayout`, which is what makes a
// link in a cell a link on both surfaces.
struct TableLayout {
  struct Cell {
    InlineLayout content;
    markdown::Align align = markdown::Align::Default;
  };
  struct Row {
    std::vector<Cell> cells;
    float height = 0.0f;
    bool header = false;
  };

  std::vector<Row> rows;
  int columns = 0;
  float columnWidth = 0.0f;
  // The insets the rows were measured with, carried here rather than looked
  // up again by whoever paints: a cell drawn at a different inset from the one
  // its lines were broken to is a cell whose text sits on its own rule.
  float padX = 0.0f;
  float padY = 0.0f;
  // Every row plus the tail gap: what the block occupies.
  float height = 0.0f;
  // The row the columns are named in, repeated at the head of each page a
  // continued table lands on. `kNoHeader` when the author wrote none.
  static constexpr std::size_t kNoHeader = static_cast<std::size_t>(-1);
  std::size_t headerRow = kNoHeader;
};

// What one md4c block is set in: the face, the weight and the size its type
// asks for. `blockLineStep` is the baseline-to-baseline distance that goes
// with it -- headings from h3 up are set tighter than body copy, because a 1.5
// ratio on a 20pt line is a gap you can park a paragraph in.
RunStyle blockRunStyle(const RenderContext& context, const markdown::Block& block);
float blockLineStep(const RenderContext& context, const RunStyle& style);

// Shapes and breaks a run of inlines to `width`, in `base`'s face and size.
// `baseRole` is the ink unmarked text takes: `Body` is the surface's own text
// and `Muted` a step back from it, which is what a table's body rows are set
// in. Every marked-up stretch keeps the role its markup asks for.
InlineLayout layoutInlines(const RenderContext& context,
                           const std::vector<markdown::Inline>& inlines, const RunStyle& base,
                           float width, TextRole baseRole = TextRole::Body);

// The same, for a whole block, in the style its type asks for.
InlineLayout layoutBlockInlines(const RenderContext& context, const markdown::Block& block,
                                float width);

// How wide one column of a table is, given how many there are and how much
// room there is. Public because the pagination reads it too.
float tableColumnWidth(const RenderContext& context, float width, int columns);
int tableColumnCount(const markdown::Block& block);

TableLayout layoutTable(const RenderContext& context, const markdown::Block& block, float width);

// Where a footnote definition's body starts, measured from the block's left
// edge: clear of the `[label]` drawn in the gutter beside it.
inline constexpr float kFootnoteLabelGap = 12.0f;

// One block of md4c's model, laid out and placed in the rendered block's own
// space.
struct RenderedItem {
  enum class Kind {
    Inlines,
    Table,
    // A blank line md4c kept, which is vertical space and nothing else.
    Blank
  };

  Kind kind = Kind::Inlines;
  // Whichever of the two the kind names; the other is empty.
  InlineLayout inlines;
  TableLayout table;
  // The face and size the block is set in, and its baseline-to-baseline step.
  // Both are what the *measure* used, so a painter reads them here instead of
  // asking the context again -- which is also what keeps the paint free of the
  // context's two `std::function`s: building those per visible block per frame
  // is two heap allocations sixty times a second for something the paint never
  // calls.
  RunStyle style;
  float lineStep = 0.0f;
  // The `[label]` a footnote definition wears in the gutter beside its body,
  // so a reader scanning the bottom of a note can tell which reference each
  // body belongs to without counting. Empty for every other kind of block.
  std::string gutterLabel;
  float gutterWidth = 0.0f;
  float top = 0.0f;
  float height = 0.0f;
};

// One atom of a rendered block, for a surface that has to break the block
// across pages.
//
// A table's rows are its atoms, which is what lets a table longer than a page
// become several pages of table rather than one page and a hole. Every other
// item of the render model is one atom: a paragraph inside a complex block
// either fits on the page or moves to the next one whole.
struct RenderedSlice {
  static constexpr std::size_t kWholeItem = static_cast<std::size_t>(-1);


  // In the rendered block's own space, so a surface can compare them against
  // the room it has left without knowing what is in them.
  float top = 0.0f;
  float bottom = 0.0f;
  // Which `items` entry this slice belongs to -- or, on the raw-source
  // fallback, which of `sourceLines` it is: that path has no items, and one
  // slice per line is what lets a surface page over the block the same way
  // either way.
  std::size_t item = 0;
  // The table row this slice draws, or `kWholeItem` when the slice is the
  // whole of its item.
  std::size_t row = kWholeItem;
};

// A whole `BlockKind::Complex` block: md4c's parse of it, and that parse laid
// out at one width.
//
// One value, held by the caller, because both surfaces measure the block and
// then draw it -- the note page's layout asks for a height when the block is
// relaid and the paint asks again every frame -- and shaping it twice is
// shaping every table on screen twice per frame. Re-laying it out is a
// no-op while the width has not moved.
struct RenderedBlock {
  markdown::Document parsed;
  bool haveParse = false;
  // Negative until it has been laid out at some width.
  float width = -1.0f;
  // md4c renders a few constructs -- a lone footnote definition, some raw HTML
  // -- to nothing at all. Then the block falls back to its own source, one
  // line per row, which is what keeps it visible rather than silently dropped.
  bool rendersNothing = false;
  std::vector<std::string> sourceLines;
  // What those lines are set in, and the step between them.
  RunStyle sourceStyle;
  float sourceLineStep = 0.0f;
  std::vector<RenderedItem> items;
  // In order, and they tile `[padTop, height - padBottom)`. Never empty for a
  // block with any content, which is what a surface's pagination relies on.
  std::vector<RenderedSlice> slices;
  float height = 0.0f;

  // Whether this is already laid out at `width`. Asked before a context is
  // built, because building one is two `std::function`s and the answer is
  // usually yes -- a paint asks per visible block per frame.
  bool laidOutAt(float other) const {
    return width >= 0.0f && other - width < 0.01f && width - other < 0.01f;
  }
};

// md4c's parse of one complex block.
//
// md4c drops a footnote definition that nothing refers to, and a complex block
// is parsed on its own -- so a definition parsed by itself comes back empty and
// falls through to the raw-source fallback. Parsing it behind a synthetic
// reference to its own label is what makes md4c keep it; the reference's own
// paragraph is then dropped, which is why the result starts at the first
// `Footnote` block rather than at the first block.
//
// Here rather than in either surface because both need it and only one had it:
// the same note showed a footnote definition as a footnote on screen and as
// grey monospace source in an export.
markdown::Document parseRenderBlock(markdown::MarkdownParser& parser, std::string_view source);

// Lays `out` out at `width`, doing nothing when it is already laid out there.
// `source` is the block's own bytes, for the fallback lines.
void layoutRenderedBlock(const RenderContext& context, std::string_view source, float width,
                         RenderedBlock& out);

// What starting a page at `slice` costs above the slices themselves.
//
// A table continued onto a second page repeats the row its columns are named
// in, because a reader who has to turn back to see what the columns are is
// reading two pages to read one. That row is drawn twice, so the height of a
// page of table is *not* the sum of the rows on it -- which is why this is a
// question about where the page starts rather than a number stored on the
// block, and why TD-40 stood open for as long as the interface reported one
// height per table.
//
// Zero for every slice that is not a continued table's first row on a page,
// which is almost all of them.
float repeatedHeaderHeight(const RenderedBlock& block, std::size_t slice);

}
