#include "app/MarkdownBlocks.h"

#include "app/PageView.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "ui/DocRuns.h"
#include "ui/DocStyle.h"
#include "ui/Metrics.h"
#include "ui/Painter.h"
#include "ui/Theme.h"

#include <algorithm>
#include <string>
#include <vector>

namespace micronotes::app {

using ui::fill;
using ui::Rect;
using ui::stroke;
using ui::TextRenderer;
using ui::theme;

namespace {

// What the screen lays an md4c block out with. The measure and the line height
// are the note page's own -- `documentMetrics` -- so a paragraph inside a
// table breaks where the same paragraph outside one would, and the type scale
// is the reader's.
doc::RenderContext renderContext(TextRenderer& text, UiRuntime& ui) {
  doc::RenderContext context;
  context.metrics = documentMetrics(text);
  context.type = documentTypeMetrics();
  context.wikiLinkResolves = [&ui](std::string_view target) {
    return wikiLinkResolves(ui, target);
  };
  return context;
}

// The block's own bytes, which is what its entry is cached under.
std::string_view complexSource(UiRuntime& ui, const doc::SourceBlock& block) {
  const std::string_view source = ui.editor.text();
  const std::size_t start = std::min(block.start, source.size());
  const std::size_t end = std::min(block.end(), source.size());
  return source.substr(start, end - start);
}

// The block, parsed and laid out at `width`.
//
// Looked up through a view. The key used to be built as a `std::string` on
// every call, which is twice per such block per frame -- once to measure it and
// once to draw it -- so a note with a table in view allocated and freed a copy
// of that table's source sixty times a second to find a parse it already had.
//
// No eviction here: what is dead is decided by the note, not by how many
// entries happen to have accumulated, and the note is not in scope from inside
// a layout pass. `sweepComplexCache` answers it once a frame.
const doc::RenderedBlock& renderedBlock(TextRenderer& text, UiRuntime& ui,
                                        const doc::SourceBlock& block, float width) {
  const std::string_view source = complexSource(ui, block);
  doc::RenderedBlock& entry = ui.complexRenders.entry(source);
  if(!entry.haveParse) {
    entry.parsed = doc::parseRenderBlock(ui.parser, source);
    entry.haveParse = true;
  }
  if(!entry.laidOutAt(width)) {
    doc::layoutRenderedBlock(renderContext(text, ui), source, width, entry);
  }
  return entry;
}

// One laid-out run of inline content, painted at `x, y`.
//
// The run loop itself is `ui::paintRuns`, which the note page's own blocks go
// through as well: this used to be a second copy of it, and the two had
// drifted -- that was TD-43. What is left here is the two things this path
// supplies rather than decides: where the layout's origin is, and that
// unmarked text in a rendered block is the page's own body ink.
void drawInlineLayout(SDL_Renderer* renderer, TextRenderer& text,
                      std::vector<ui::LinkRegion>* links, const doc::InlineLayout& content,
                      float x, float y) {
  ui::RunPaint paint;
  paint.x = x;
  paint.y = y;
  paint.bodyInk = ui::inkFor(theme(), doc::TextRole::Body);
  paint.links = links;
  ui::paintRuns(renderer, text, content.layout, paint);
}

void drawTableLayout(SDL_Renderer* renderer, TextRenderer& text,
                     std::vector<ui::LinkRegion>* links, const doc::TableLayout& table, float x,
                     float y) {
  float top = y;
  for(const auto& row : table.rows) {
    float cellX = x;
    for(int column = 0; column < table.columns; ++column) {
      const Rect cellRect {cellX, top, table.columnWidth, row.height};
      fill(renderer, cellRect,
           row.header ? theme().tableHeaderBackground : theme().editorBackground);
      stroke(renderer, cellRect, theme().border);
      if(column < static_cast<int>(row.cells.size())) {
        const auto& cell = row.cells[static_cast<std::size_t>(column)];
        // The alignment the table's delimiter row asked for, applied to the
        // cell's own content width rather than to the column, and floored at
        // the left inset so a wide cell still starts inside its own box.
        float textX = cellX + table.padX;
        const float contentWidth = cell.content.width;
        if(cell.align == markdown::Align::Right) {
          textX = cellX + table.columnWidth - table.padX - contentWidth;
        } else if(cell.align == markdown::Align::Center) {
          textX = cellX + (table.columnWidth - contentWidth) / 2.0f;
        }
        drawInlineLayout(renderer, text, links, cell.content,
                         std::max(cellX + table.padX, textX), top + table.padY);
      }
      cellX += table.columnWidth;
    }
    top += row.height;
  }
}

}

void sweepComplexCache(UiRuntime& ui, doc::BlockSpan blocks, std::string_view source) {
  if(!ui.complexRenders.sweepDue()) return;
  std::vector<std::string_view> live;
  for(const auto& block : blocks) {
    if(block.kind != doc::BlockKind::Complex) continue;
    const std::size_t start = std::min(block.start, source.size());
    const std::size_t end = std::min(block.end(), source.size());
    live.push_back(source.substr(start, end - start));
  }
  ui.complexRenders.sweep(std::move(live));
}

float measureComplexBlock(TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block,
                          float width) {
  return renderedBlock(text, ui, block, width).height;
}

// Nothing here reads the `doc::RenderContext`: every number the paint needs is
// on the layout, put there by the measure. That is not only tidier, it is what
// keeps the per-frame path free of the context's two `std::function`s -- this
// runs once per visible complex block per frame.
void drawComplexBlock(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui,
                      const doc::SourceBlock& block, Rect rect) {
  const doc::RenderedBlock& rendered = renderedBlock(text, ui, block, rect.w);

  // A block md4c rendered to nothing at all falls back to its own source, in
  // the monospace face, which keeps it visible rather than silently dropped.
  if(rendered.rendersNothing) {
    const ui::TextStyle style = ui::textStyleFor(rendered.sourceStyle);
    float y = rect.y + (rendered.slices.empty() ? 0.0f : rendered.slices.front().top);
    for(const auto& line : rendered.sourceLines) {
      text.draw(ui::ellipsizeToWidth(text, line, static_cast<int>(rect.w), style), rect.x, y,
                theme().textSecondary, style);
      y += rendered.sourceLineStep;
    }
    return;
  }

  for(const auto& item : rendered.items) {
    const float top = rect.y + item.top;
    switch(item.kind) {
      case doc::RenderedItem::Kind::Blank:
        break;
      case doc::RenderedItem::Kind::Table:
        drawTableLayout(renderer, text, &ui.linkRegions, item.table, rect.x, top);
        break;
      case doc::RenderedItem::Kind::Inlines:
        // A footnote definition wears its own label in the gutter, so a reader
        // scanning the bottom of a note can tell which reference each body
        // belongs to without counting.
        if(!item.gutterLabel.empty()) {
          text.draw(item.gutterLabel, rect.x, top, theme().accent);
        }
        drawInlineLayout(renderer, text, &ui.linkRegions, item.inlines,
                         rect.x + item.gutterWidth, top);
        break;
    }
  }
}

}
