#include "doc/RenderLayout.h"

#include "doc/Flow.h"
#include "doc/Tokenize.h"
#include "doc/WikiLink.h"

#include "core/util/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace micronotes::doc {
namespace {

using microcore::markdown::Block;
using microcore::markdown::BlockType;
using microcore::markdown::Document;
using microcore::markdown::Inline;
using microcore::markdown::InlineType;

// The flattened bytes of a run of inlines and what each byte looks like: the
// two inputs `appendContentTokens` wants. Building an attribute per byte is
// what the note page's own path does; md4c hands the spans back already
// separated, so filling it is an append rather than the interval merge
// `applyInlineSpans` has to do.
struct Flattened {
  std::string text;
  std::vector<Attr> attrs;
  std::vector<std::string> links;
};

void appendSpan(Flattened& out, std::string_view text, const Attr& attr) {
  out.text += text;
  out.attrs.insert(out.attrs.end(), text.size(), attr);
}

int linkFor(Flattened& out, std::string target) {
  out.links.push_back(std::move(target));
  return static_cast<int>(out.links.size()) - 1;
}

// Whether two stretches differ only in their bytes, so one can be appended to
// the other. A stretch with a link is never joined: a link is a unit.
bool sameAppearance(const Attr& a, const Attr& b) {
  return a.link < 0 && b.link < 0 && a == b;
}

// Splits a stretch of plain text on the `[[wikilinks]]` in it, appending the
// literal pieces and the links in order. `attr` carries whatever emphasis the
// text already had, so `*[[a]]*` stays italic.
void appendWithWikiLinks(Flattened& out, std::string_view text, const Attr& attr,
                         const RenderContext& context) {
  // The overwhelming majority of stretches carry no `[[` at all, and this is
  // on the per-block path, so the cheap test comes first.
  if(text.find("[[") == std::string_view::npos) {
    appendSpan(out, text, attr);
    return;
  }
  std::size_t copied = 0;
  while(const auto span = findWikiLink(text, copied)) {
    if(span->start > copied) {
      appendSpan(out, text.substr(copied, span->start - copied), attr);
    }
    // A link to a note that is not there yet is an offer, not an error -- the
    // same distinction, and the same two colours, the note page draws.
    const bool resolves = !context.wikiLinkResolves || context.wikiLinkResolves(span->target);
    Attr link = attr;
    link.role = resolves ? TextRole::WikiLink : TextRole::WikiLinkUnresolved;
    link.link = linkFor(out, span->target);
    appendSpan(out, span->label, link);
    copied = span->end;
  }
  if(copied < text.size()) {
    appendSpan(out, text.substr(copied), attr);
  }
}

// md4c's inlines, flattened.
//
// Adjacent plain-text stretches are joined before the wikilink scan and only
// then split again, because md4c breaks its text at every `[`: it tries to
// read `[[Deep Note]]` as a link, finds no `(`, and hands back the pieces
// `"A ["`, `"["`, `"Deep Note]] ..."`. No single one of those contains a `[[`,
// so a scan that looked at them one at a time could never find one -- which is
// why the reading pane once showed the raw brackets of every link between
// notes.
Flattened flatten(const std::vector<Inline>& inlines, TextRole baseRole,
                  const RenderContext& context) {
  Flattened out;
  std::string pendingText;
  Attr pendingAttr;
  bool havePending = false;

  const auto flush = [&]() {
    if(!havePending) return;
    appendWithWikiLinks(out, pendingText, pendingAttr, context);
    pendingText.clear();
    pendingAttr = Attr {};
    havePending = false;
  };

  for(const auto& item : inlines) {
    if(item.type == InlineType::Image) continue;
    std::string_view text = item.text;
    Attr attr;
    attr.role = baseRole;
    attr.strong = item.strong;
    attr.italic = item.emphasis;
    attr.strike = item.strikethrough;
    // Only a stretch md4c handed back as text can be hiding a wikilink. A
    // link's label, a code span and raw HTML are all what they say they are.
    bool splittable = false;
    std::string synthesized;
    switch(item.type) {
      case InlineType::Link:
        if(item.text.empty()) text = item.target;
        attr.role = TextRole::Link;
        attr.link = linkFor(out, item.target);
        break;
      case InlineType::Code:
        attr.mono = true;
        attr.role = TextRole::Code;
        break;
      case InlineType::Emphasis:
        attr.italic = true;
        splittable = true;
        break;
      case InlineType::Strong:
        attr.strong = true;
        splittable = true;
        break;
      case InlineType::Strikethrough:
        attr.strike = true;
        splittable = true;
        break;
      case InlineType::FootnoteRef:
        synthesized = "[" + item.text + "]";
        text = synthesized;
        attr.role = TextRole::Link;
        attr.link = linkFor(out, "#fn-" + item.text);
        break;
      case InlineType::Html:
        // Markup being shown as markup rather than read as prose, which is
        // what `Marker` means and what both surfaces already drew it in.
        attr.role = TextRole::Marker;
        break;
      case InlineType::Text:
        splittable = true;
        break;
      case InlineType::Image:
        break;
    }
    if(text.empty()) continue;
    if(!splittable) {
      flush();
      appendSpan(out, text, attr);
      continue;
    }
    if(havePending && sameAppearance(pendingAttr, attr)) {
      pendingText += text;
      continue;
    }
    flush();
    pendingText.assign(text);
    pendingAttr = attr;
    havePending = true;
  }
  flush();
  return out;
}

}

RunStyle blockRunStyle(const RenderContext& context, const Block& block) {
  RunStyle style;
  switch(block.type) {
    case BlockType::Heading:
      style.strong = true;
      style.size = context.type.heading[std::clamp(block.level, 1, 6) - 1];
      break;
    case BlockType::Code:
      style.mono = true;
      style.size = context.type.mono;
      break;
    default:
      style.size = context.type.body;
      break;
  }
  return style;
}

float blockLineStep(const RenderContext& context, const RunStyle& style) {
  if(context.metrics.lineHeight) return context.metrics.lineHeight(style);
  // The screen's rule, for a context that gave no line-height function -- a
  // test's, mostly: a heading from h3 up is set tighter than body copy,
  // because a 1.5 ratio on a 20pt line is a gap you can park a paragraph in.
  const float size = style.size > 0.0f ? style.size : context.type.body;
  const float ratio = size >= context.type.heading[2] ? 1.25f : context.type.lineHeightRatio;
  return std::round(size * ratio);
}

InlineLayout layoutInlines(const RenderContext& context, const std::vector<Inline>& inlines,
                           const RunStyle& base, float width, TextRole baseRole) {
  InlineLayout out;
  Flattened flat = flatten(inlines, baseRole, context);
  out.text = std::move(flat.text);
  out.layout.links = std::move(flat.links);

  // One group: a hard break inside the block arrives as a `\n` of its own and
  // `appendContentTokens` turns it into the break token the flow closes a line
  // on, so there is nothing here for a second group to say.
  //
  // The group and the scratch are allocated here rather than borrowed, which
  // is the opposite of what `FlowScratch`'s own comment asks for -- and it is
  // the right call at this end. That comment is about the note page, where the
  // flow runs over every block of the buffer on every keystroke; this runs
  // once per table per change of width, behind a memo, and threading two
  // buffers through the signature to save a handful of allocations on a path
  // that is not hot would be optimising by guess.
  std::vector<LineGroup> groups(1);
  const float lineHeight = blockLineStep(context, base);
  if(!out.text.empty()) {
    appendContentTokens(out.text, 0, out.text.size(), flat.attrs, base, context.type.mono,
                        groups.front());
  }
  FlowScratch scratch;
  FlowGeometry geometry;
  geometry.base = 0;
  geometry.textLeft = 0.0f;
  geometry.width = std::max(4.0f, width);
  geometry.lineHeight = lineHeight;
  geometry.top = 0.0f;
  geometry.wrap = true;
  Flow flow(context.metrics, geometry, out.layout, scratch);
  flow.run(groups, groups.size());
  out.layout.height = std::max(lineHeight, flow.bottom());
  for(const auto& run : out.layout.runs) {
    out.width = std::max(out.width, run.rect.x + run.rect.w);
  }
  return out;
}

InlineLayout layoutBlockInlines(const RenderContext& context, const Block& block, float width) {
  return layoutInlines(context, block.inlines, blockRunStyle(context, block), width);
}

int tableColumnCount(const Block& block) {
  int count = 0;
  for(const auto& row : block.tableRows) {
    count = std::max(count, static_cast<int>(row.cells.size()));
  }
  return count;
}

float tableColumnWidth(const RenderContext& context, float width, int columns) {
  const int cols = std::max(1, columns);
  const float even = (width - static_cast<float>(cols + 1)) / static_cast<float>(cols);
  return std::max(context.table.minCellWidth, even);
}

TableLayout layoutTable(const RenderContext& context, const Block& block, float width) {
  TableLayout out;
  out.columns = std::max(1, tableColumnCount(block));
  out.columnWidth = tableColumnWidth(context, width, out.columns);
  out.padX = context.table.padX;
  out.padY = context.table.padY;
  const float textWidth = std::max(4.0f, out.columnWidth - out.padX * 2.0f);

  for(const auto& row : block.tableRows) {
    TableLayout::Row laid;
    laid.header = row.header;
    // The header names the columns, so it is the one row that reads as the
    // page's own text; the body sits a step back from it.
    const TextRole role = row.header ? TextRole::Body : TextRole::Muted;
    RunStyle style;
    style.size = context.type.body;
    style.strong = row.header;
    float tallest = blockLineStep(context, style);
    for(const auto& cell : row.cells) {
      TableLayout::Cell laidCell;
      laidCell.align = cell.align;
      laidCell.content = layoutInlines(context, cell.inlines, style, textWidth, role);
      tallest = std::max(tallest, laidCell.content.height());
      laid.cells.push_back(std::move(laidCell));
    }
    laid.height = tallest + out.padY * 2.0f;
    if(row.header && out.headerRow == TableLayout::kNoHeader) {
      out.headerRow = out.rows.size();
    }
    out.height += laid.height;
    out.rows.push_back(std::move(laid));
  }
  out.height += context.table.tailGap;
  return out;
}

namespace {

// The label of `[^label]: ...`, or empty when the source is not a footnote
// definition.
std::string_view footnoteLabelOf(std::string_view source) {
  if(source.size() < 4 || source[0] != '[' || source[1] != '^') return {};
  const auto close = source.find("]:");
  if(close == std::string_view::npos || close <= 2) return {};
  const auto label = source.substr(2, close - 2);
  return label.find('\n') == std::string_view::npos ? label : std::string_view {};
}

// Whether md4c rendered the source to nothing at all.
bool rendersNothing(const Document& document) {
  for(const auto& block : document.blocks) {
    if(block.type == BlockType::Table) return false;
    if(!microcore::markdown::plainText(block.inlines).empty()) return false;
  }
  return true;
}

}

Document parseRenderBlock(markdown::MarkdownParser& parser, std::string_view source) {
  const auto label = footnoteLabelOf(source);
  if(label.empty()) return parser.parse(source);
  std::string withReference;
  withReference.reserve(source.size() + label.size() + 6);
  withReference += "[^";
  withReference += label;
  withReference += "]\n\n";
  withReference += source;
  auto document = parser.parse(withReference);
  auto first = document.blocks.begin();
  while(first != document.blocks.end() && first->type != BlockType::Footnote) ++first;
  if(first == document.blocks.end()) return parser.parse(source);
  document.blocks.erase(document.blocks.begin(), first);
  return document;
}

void layoutRenderedBlock(const RenderContext& context, std::string_view source, float width,
                         RenderedBlock& out) {
  if(out.laidOutAt(width)) return;
  out.width = width;
  out.items.clear();
  out.sourceLines.clear();
  out.height = 0.0f;
  out.rendersNothing = rendersNothing(out.parsed);

  if(out.rendersNothing) {
    out.sourceStyle = RunStyle {};
    out.sourceStyle.mono = true;
    out.sourceStyle.size = context.type.mono;
    out.sourceLineStep = blockLineStep(context, out.sourceStyle);
    out.sourceLines = util::splitLines(source);
    // One slice per line, so a fallback longer than a page breaks between its
    // lines the way a table breaks between its rows -- and so that a surface
    // paginating this block has the same thing to page over either way.
    float lineTop = context.spacing.padTop;
    for(std::size_t i = 0; i < out.sourceLines.size(); ++i) {
      out.slices.push_back({lineTop, lineTop + out.sourceLineStep, i, RenderedSlice::kWholeItem});
      lineTop += out.sourceLineStep;
    }
    out.height = lineTop + context.spacing.padBottom;
    return;
  }

  RunStyle body;
  body.size = context.type.body;
  const float blankStep = blockLineStep(context, body);

  float y = context.spacing.padTop;
  for(const auto& block : out.parsed.blocks) {
    RenderedItem item;
    item.top = y;
    item.style = blockRunStyle(context, block);
    item.lineStep = blockLineStep(context, item.style);
    if(block.type == BlockType::Table) {
      item.kind = RenderedItem::Kind::Table;
      item.table = layoutTable(context, block, width);
      item.height = item.table.height + context.spacing.tableGap;
    } else if(block.type == BlockType::BlankLine) {
      item.kind = RenderedItem::Kind::Blank;
      item.height = blankStep;
    } else {
      item.kind = RenderedItem::Kind::Inlines;
      if(block.type == BlockType::Footnote) {
        item.gutterLabel =
          "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
        const float labelWidth =
          context.metrics.measure ? context.metrics.measure(item.gutterLabel, item.style) : 0.0f;
        item.gutterWidth = labelWidth + kFootnoteLabelGap;
      }
      item.inlines = layoutBlockInlines(context, block, width - item.gutterWidth);
      item.height = item.inlines.height() + context.spacing.blockGap;
    }
    y += item.height;
    out.items.push_back(std::move(item));
  }
  out.height = y + context.spacing.padBottom;

  for(std::size_t index = 0; index < out.items.size(); ++index) {
    const RenderedItem& item = out.items[index];
    if(item.kind != RenderedItem::Kind::Table) {
      out.slices.push_back({item.top, item.top + item.height, index, RenderedSlice::kWholeItem});
      continue;
    }
    float top = item.top;
    for(std::size_t row = 0; row < item.table.rows.size(); ++row) {
      const float height = item.table.rows[row].height;
      out.slices.push_back({top, top + height, index, row});
      top += height;
    }
    // The tail gap belongs to the last row rather than to a slice of its own:
    // a slice with nothing in it would be a page break the reader can land on.
    if(!out.slices.empty() && !item.table.rows.empty()) {
      out.slices.back().bottom = item.top + item.height;
    }
  }
}

float repeatedHeaderHeight(const RenderedBlock& block, std::size_t slice) {
  if(slice >= block.slices.size()) return 0.0f;
  const RenderedSlice& at = block.slices[slice];
  if(at.row == RenderedSlice::kWholeItem) return 0.0f;
  const RenderedItem& item = block.items[at.item];
  const std::size_t header = item.table.headerRow;
  if(header == TableLayout::kNoHeader) return 0.0f;
  // The header itself, and the row after it that would be the page's first
  // anyway, are drawn where they are.
  if(at.row <= header) return 0.0f;
  return item.table.rows[header].height;
}

}
