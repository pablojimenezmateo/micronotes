#include "export/PdfComplex.h"

#include "export/PdfPage.h"

#include <algorithm>
#include <string>
#include <utility>

namespace micronotes::exporting {
namespace {

using microcore::markdown::Align;
using microcore::pdf::PdfContent;

// Where a cell's content starts across the column: the alignment its
// delimiter row asked for, applied to the content's own width and floored at
// the left inset so a cell wider than its column still starts inside its box.
float cellTextLeft(const doc::TableLayout& table, const doc::TableLayout::Cell& cell,
                   float cellLeft) {
  const float inset = cellLeft + table.padX;
  if(cell.align == Align::Right) {
    return std::max(inset, cellLeft + table.columnWidth - table.padX - cell.content.width);
  }
  if(cell.align == Align::Center) {
    return std::max(inset, cellLeft + (table.columnWidth - cell.content.width) / 2.0f);
  }
  return inset;
}

}

doc::RenderContext PdfComplex::context() const {
  doc::RenderContext context;
  context.metrics = printMetrics(*faces_, *document_);
  context.type = printTypeMetrics();
  // The screen's paddings and gaps, in points. Taken from the structs' own
  // defaults rather than written out again, so a change to the screen's table
  // carries to the page instead of the two drifting.
  const doc::TableGeometry screenTable;
  context.table.padX = screenTable.padX * kPrintScale;
  context.table.padY = screenTable.padY * kPrintScale;
  context.table.minCellWidth = screenTable.minCellWidth * kPrintScale;
  context.table.tailGap = screenTable.tailGap * kPrintScale;
  const doc::RenderSpacing screenSpacing;
  context.spacing.padTop = screenSpacing.padTop * kPrintScale;
  context.spacing.padBottom = screenSpacing.padBottom * kPrintScale;
  context.spacing.blockGap = screenSpacing.blockGap * kPrintScale;
  context.spacing.tableGap = screenSpacing.tableGap * kPrintScale;
  context.wikiLinkResolves = wikiResolves_;
  return context;
}

void PdfComplex::resolveWikiLinksWith(std::function<bool(std::string_view)> resolves) {
  if(!wikiResolves_ && !resolves) return;
  wikiResolves_ = std::move(resolves);
  // Every layout was shaped against the old answer, and a role is baked into
  // a run. Cheaper to drop them than to reason about which held a wikilink.
  cache_.clear();
}

doc::RenderedBlock& PdfComplex::entryFor(std::string_view source, float width) {
  doc::RenderedBlock& entry = cache_[std::string(source)];
  if(!entry.haveParse) {
    entry.parsed = doc::parseRenderBlock(parser_, source);
    entry.haveParse = true;
  }
  if(!entry.laidOutAt(width)) doc::layoutRenderedBlock(context(), source, width, entry);
  return entry;
}

const doc::RenderedBlock& PdfComplex::layoutOf(std::string_view source, float width) {
  return entryFor(source, width);
}

void PdfComplex::paintTableRow(PdfContent& content, const BlockInk& blockInk,
                               const doc::TableLayout& table, std::size_t rowIndex, float x,
                               float y) {
  if(rowIndex >= table.rows.size()) return;
  const doc::TableLayout::Row& row = table.rows[rowIndex];
  const ui::Theme& theme = ink();

  float cellX = x;
  for(int column = 0; column < table.columns; ++column) {
    content.fillRect(cellX, y, table.columnWidth, row.height,
                     row.header ? theme.tableHeaderBackground : kPaper);
    content.strokeRect(cellX, y, table.columnWidth, row.height, 0.5f, theme.border);
    if(column < static_cast<int>(row.cells.size())) {
      const auto& cell = row.cells[static_cast<std::size_t>(column)];
      RunPaint paint;
      paint.x = cellTextLeft(table, cell, cellX);
      paint.y = y + table.padY;
      paint.bodyInk = ui::inkFor(theme, row.header ? doc::TextRole::Body : doc::TextRole::Muted);
      paintRuns(content, blockInk, cell.content.layout, paint);
    }
    cellX += table.columnWidth;
  }
}

void PdfComplex::paint(PdfContent& content, const ComplexSlice& at) {
  doc::RenderedBlock& block = entryFor(at.source, at.width);
  const ui::Theme& theme = ink();
  BlockInk blockInk;
  blockInk.document = document_;
  blockInk.faces = faces_;
  blockInk.links = at.links;
  const float x = at.x;
  std::size_t to = at.to;

  // A block md4c rendered to nothing at all falls back to its own source, one
  // line per slice, in the monospace face.
  if(block.rendersNothing) {
    const doc::RunStyle& mono = block.sourceStyle;
    const int slot = faces_->slot(mono);
    auto& font = document_->font(slot);
    float top = at.y;
    for(std::size_t i = at.from; i < to && i < block.sourceLines.size(); ++i) {
      content.text(font, slot, mono.size, x,
                   top + baselineIn(font, block.sourceLineStep, mono.size), block.sourceLines[i],
                   theme.textSecondary);
      top += block.sourceLineStep;
    }
    return;
  }

  to = std::min(to, block.slices.size());
  if(at.from >= to) return;

  float top = at.y;
  // The header row of a table this page did not start, drawn above the rows
  // that did land here, so the columns are named on every page they run over.
  const float header = doc::repeatedHeaderHeight(block, at.from);
  if(header > 0.0f) {
    const doc::RenderedItem& item = block.items[block.slices[at.from].item];
    paintTableRow(content, blockInk, item.table, item.table.headerRow, x, top);
    top += header;
  }

  for(std::size_t index = at.from; index < to; ++index) {
    const doc::RenderedSlice& slice = block.slices[index];
    const doc::RenderedItem& item = block.items[slice.item];
    if(slice.row != doc::RenderedSlice::kWholeItem) {
      paintTableRow(content, blockInk, item.table, slice.row, x, top);
    } else if(item.kind == doc::RenderedItem::Kind::Inlines) {
      if(!item.gutterLabel.empty()) {
        const int slot = faces_->slot(item.style);
        auto& font = document_->font(slot);
        content.text(font, slot, item.style.size, x,
                     top + baselineIn(font, item.lineStep, item.style.size), item.gutterLabel,
                     theme.accent);
      }
      RunPaint paint;
      paint.x = x + item.gutterWidth;
      paint.y = top;
      paint.bodyInk = ui::inkFor(theme, doc::TextRole::Body);
      paintRuns(content, blockInk, item.inlines.layout, paint);
    }
    top += slice.bottom - slice.top;
  }
}

}
