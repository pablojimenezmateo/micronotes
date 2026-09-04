#include "app/MarkdownBlocks.h"

#include "ui/Theme.h"
#include "ui/TextUtil.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace micronotes::app {

using ui::fill;
using ui::Rect;
using ui::stroke;
using ui::theme;
using ui::TextRenderer;

std::string blockText(const markdown::Block& block) {
  std::string out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) {
      continue;
    } else if(inlineItem.type == markdown::InlineType::Link) {
      out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      out += block.type == markdown::BlockType::Code ? inlineItem.text : "`" + inlineItem.text + "`";
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      out += "[" + inlineItem.text + "]";
    } else {
      out += inlineItem.text;
    }
  }
  return out;
}



std::string inlinePlainText(const std::vector<markdown::Inline>& inlines) {
  std::string out;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    if(inlineItem.type == markdown::InlineType::Link) out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    else if(inlineItem.type == markdown::InlineType::FootnoteRef) out += "[" + inlineItem.text + "]";
    else out += inlineItem.text;
  }
  return out;
}


ui::TextStyle blockTextStyle(const markdown::Block& block) {
  ui::TextStyle style;
  style.family = block.type == markdown::BlockType::Code ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  style.strong = block.type == markdown::BlockType::Heading;
  if(block.type == markdown::BlockType::Heading) style.size = ui::headingSize(block.level);
  else if(block.type == markdown::BlockType::Code) style.size = ui::type().mono;
  else style.size = ui::type().body;
  return style;
}

// Baseline-to-baseline distance. The font's own height is roughly 1.2x, which
// reads too tight for body copy, so the type scale's ratio wins when larger.
int lineStepFor(TextRenderer& text, const ui::TextStyle& style, float ratio) {
  const float logical = style.size > 0.0f ? style.size : ui::type().body;
  const int fromRatio = static_cast<int>(std::lround(logical * text.displayScale() * ratio));
  return std::max(text.lineHeight(style), fromRatio);
}

int blockLineStep(TextRenderer& text, const markdown::Block& block) {
  const auto style = blockTextStyle(block);
  const bool heading = block.type == markdown::BlockType::Heading;
  return lineStepFor(text, style, heading ? 1.25f : ui::type().lineHeightRatio);
}

float tableHeight(TextRenderer& text, const markdown::Block& block, float width) {
  const float rowPadY = 8.0f;
  const int cols = std::max(1, [&]() {
    int count = 0;
    for(const auto& row : block.tableRows) count = std::max(count, static_cast<int>(row.cells.size()));
    return count;
  }());
  const float cellW = std::max(48.0f, (width - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float h = 0.0f;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f), ui::type().body));
    }
    h += static_cast<float>(rowLines * (text.lineHeight() + 2)) + rowPadY * 2.0f;
  }
  return h + 10.0f;
}

void drawTable(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>& links, const markdown::Block& block, Rect rect) {
  int cols = 0;
  for(const auto& row : block.tableRows) cols = std::max(cols, static_cast<int>(row.cells.size()));
  if(cols <= 0) return;
  const float cellW = std::max(48.0f, (rect.w - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float y = rect.y;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f), ui::type().body));
    }
    const float rowH = static_cast<float>(rowLines * (text.lineHeight() + 2)) + 16.0f;
    float x = rect.x;
    for(int i = 0; i < cols; ++i) {
      const markdown::TableCell* cell = i < static_cast<int>(row.cells.size()) ? &row.cells[static_cast<std::size_t>(i)] : nullptr;
      Rect cellRect {x, y, cellW, rowH};
      fill(renderer, cellRect, row.header ? theme().tableHeaderBg : theme().tableCellBg);
      stroke(renderer, cellRect, theme().divider);
      if(cell) {
        auto runs = inlineRuns(cell->inlines, row.header ? theme().text : theme().muted);
        const auto cellText = inlinePlainText(cell->inlines);
        float textX = x + 7.0f;
        if(cell->align == markdown::Align::Right) {
          textX = std::max(textX, x + cellW - 7.0f - static_cast<float>(text.width(cellText)));
        } else if(cell->align == markdown::Align::Center) {
          textX = std::max(textX, x + (cellW - static_cast<float>(text.width(cellText))) / 2.0f);
        }
        drawInlineRuns(renderer, text, &links, runs, textX, y + 8.0f, static_cast<int>(cellW - 14.0f), lineStepFor(text, ui::TextStyle {}, ui::type().lineHeightRatio), ui::type().body);
      }
      x += cellW;
    }
    y += rowH;
  }
}

namespace {

// The block's own bytes, which is what its parse is cached under.
std::string_view complexSource(UiRuntime& ui, const doc::SourceBlock& block) {
  const std::string_view source = ui.editor.text();
  const std::size_t start = std::min(block.start, source.size());
  const std::size_t end = std::min(block.end(), source.size());
  return source.substr(start, end - start);
}

// A block the live scanner does not model is parsed on its own and rendered by
// the md4c path, so tables and raw HTML look the same everywhere.
//
// Looked up through a view. The key used to be built as a `std::string` on
// every call, which is twice per such block per frame -- once to measure it and
// once to draw it -- so a note with a table in view allocated and freed a copy
// of that table's source sixty times a second to find a parse it already had.
// Where a footnote definition's body starts, measured from the block's left
// edge: clear of the `[label]` drawn in the gutter beside it.
constexpr float kFootnoteLabelGap = 12.0f;

// The gutter label a footnote definition draws, and how wide it is. Empty for
// every other kind of block, which is what the zero width says.
float footnoteLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Footnote) return 0.0f;
  const auto label = "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
  return static_cast<float>(text.width(label)) + kFootnoteLabelGap;
}

// The label of `[^label]: ...`, or empty when the block is not a footnote
// definition.
std::string_view footnoteLabelOf(std::string_view source) {
  if(source.size() < 4 || source[0] != '[' || source[1] != '^') return {};
  const auto close = source.find("]:");
  if(close == std::string_view::npos || close <= 2) return {};
  const auto label = source.substr(2, close - 2);
  return label.find('\n') == std::string_view::npos ? label : std::string_view {};
}

// md4c drops a footnote definition that nothing refers to, and a `Complex` block
// is parsed on its own -- so a definition parsed by itself came back empty and
// fell through to the raw-source fallback below. Both surfaces showed the
// author's `[^label]: ...` in grey monospace instead of a footnote.
//
// Parsing it behind a synthetic reference to its own label is what makes md4c
// keep it. The reference's own paragraph is then dropped, which is why the
// result starts at the first `Footnote` block rather than at the first block.
markdown::Document parseComplex(UiRuntime& ui, std::string_view source) {
  const auto label = footnoteLabelOf(source);
  if(label.empty()) return ui.parser.parse(source);
  std::string withReference;
  withReference.reserve(source.size() + label.size() + 6);
  withReference += "[^";
  withReference += label;
  withReference += "]\n\n";
  withReference += source;
  auto document = ui.parser.parse(withReference);
  auto first = document.blocks.begin();
  while(first != document.blocks.end() && first->type != markdown::BlockType::Footnote) ++first;
  if(first == document.blocks.end()) return ui.parser.parse(source);
  document.blocks.erase(document.blocks.begin(), first);
  return document;
}

const markdown::Document& complexDocument(UiRuntime& ui, const doc::SourceBlock& block) {
  const std::string_view key = complexSource(ui, block);
  auto found = ui.complexCache.find(key);
  if(found == ui.complexCache.end()) {
    if(ui.complexCache.size() > 64) ui.complexCache.clear();
    found = ui.complexCache.emplace(std::string(key), parseComplex(ui, key)).first;
  }
  return found->second;
}

// md4c renders a few constructs (a lone footnote definition, say) to nothing at
// all. Falling back to the source keeps such a block visible and editable.
bool complexRendersNothing(const markdown::Document& document) {
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) return false;
    if(!blockText(item).empty()) return false;
  }
  return true;
}

std::vector<std::string> complexSourceLines(UiRuntime& ui, const doc::SourceBlock& block) {
  return ui::splitLines(complexSource(ui, block));
}

}

float measureComplexBlock(TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block, float width) {
  const auto& document = complexDocument(ui, block);
  if(complexRendersNothing(document)) {
    ui::TextStyle mono;
    mono.family = ui::FontFamily::Mono;
    mono.size = ui::type().mono;
    return static_cast<float>(complexSourceLines(ui, block).size() * lineStepFor(text, mono, 1.5f)) + 20.0f;
  }
  float height = 10.0f;
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) {
      height += tableHeight(text, item, width) + 12.0f;
    } else if(item.type == markdown::BlockType::BlankLine) {
      height += static_cast<float>(text.lineHeight());
    } else {
      const auto runs = inlineRuns(item, theme().text);
      const auto style = blockTextStyle(item);
      const float indent = footnoteLabelWidth(text, item);
      height += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(width - indent), style.size) * blockLineStep(text, item)) + 6.0f;
    }
  }
  return height + 10.0f;
}

void drawComplexBlock(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block, Rect rect) {
  const auto& document = complexDocument(ui, block);
  if(complexRendersNothing(document)) {
    ui::TextStyle mono;
    mono.family = ui::FontFamily::Mono;
    mono.size = ui::type().mono;
    const int step = lineStepFor(text, mono, 1.5f);
    float y = rect.y + 10.0f;
    for(const auto& line : complexSourceLines(ui, block)) {
      text.draw(ellipsizeToWidth(text, line, static_cast<int>(rect.w), mono), rect.x, y, theme().muted, mono);
      y += static_cast<float>(step);
    }
    return;
  }
  float y = rect.y + 10.0f;
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) {
      const float height = tableHeight(text, item, rect.w);
      drawTable(renderer, text, ui.linkRegions, item, {rect.x, y, rect.w, height});
      y += height + 12.0f;
    } else if(item.type == markdown::BlockType::BlankLine) {
      y += static_cast<float>(text.lineHeight());
    } else {
      const auto runs = inlineRuns(item, theme().text);
      const auto style = blockTextStyle(item);
      const int step = blockLineStep(text, item);
      // A footnote definition wears its own label in the gutter, so a reader
      // scanning the bottom of a note can tell which reference each body
      // belongs to without counting.
      const float indent = footnoteLabelWidth(text, item);
      if(indent > 0.0f) {
        const auto label = item.footnoteLabel.empty() ? std::string("*") : item.footnoteLabel;
        text.draw("[" + label + "]", rect.x, y, theme().accent);
      }
      // Where the draw left off, rather than a second full inline layout of it.
      y = drawInlineRuns(renderer, text, &ui.linkRegions, runs, rect.x + indent, y,
                         static_cast<int>(rect.w - indent), step, style.size) +
          static_cast<float>(step) + 6.0f;
    }
  }
}
}
