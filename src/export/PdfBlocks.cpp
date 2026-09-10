#include "export/PdfBlocks.h"

#include "doc/LinkTarget.h"
#include "export/PdfPage.h"
#include "ui/DocRuns.h"
#include "ui/DocStyle.h"
#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace micronotes::exporting {
namespace {

using microcore::pdf::PdfContent;
using microcore::pdf::PdfFont;

// The ink a run takes, which is `ui::inkFor` read against the light palette --
// the same table the screen inks with, so a role added to `doc::TextRole` can
// never be drawn on screen and left uninked on the page.
SDL_Color inkFor(doc::TextRole role, doc::BlockKind kind) {
  return ui::inkFor(ink(), role, kind);
}

// A tick, drawn rather than typeset, exactly as `ui::Glyphs` draws the one on
// screen: two strokes, because there is no character in the body face that is
// the right weight beside 11pt text.
void drawTick(PdfContent& content, float x, float y, float size, SDL_Color color) {
  const float weight = std::max(0.6f, size * 0.11f);
  content.line(x + size * 0.24f, y + size * 0.52f, x + size * 0.43f, y + size * 0.72f, weight,
               color);
  content.line(x + size * 0.43f, y + size * 0.72f, x + size * 0.78f, y + size * 0.28f, weight,
               color);
}

// The list marker, the ordinal or the task box: whatever stands in for the
// marker text, which is never shown.
void paintListChrome(PdfContent& content, const BlockInk& ink, const doc::SourceBlock& block,
                     const doc::BlockLayout& layout, float left, float lineTop, float lineHeight) {
  const ui::Theme& theme = exporting::ink();
  const doc::TypeMetrics type = printTypeMetrics();
  doc::RunStyle style;
  style.size = type.body;
  auto& font = ink.document->font(ink.faces->slot(style));

  if(block.kind == doc::BlockKind::Bullet) {
    // A drawn disc rather than the bullet character: the face's own bullet is
    // a different size and a different height above the baseline in each of
    // the four sans faces, and this one is neither.
    const float radius = std::max(0.9f, type.body * 0.105f);
    content.fillCircle(left + type.body * 0.42f, lineTop + lineHeight / 2.0f, radius,
                       theme.textSecondary);
    return;
  }
  if(block.kind == doc::BlockKind::Ordered) {
    const auto label = std::to_string(block.ordinal > 0 ? block.ordinal : 1) + ".";
    const float baseline = lineTop + baselineIn(font, lineHeight, style.size);
    content.text(font, ink.faces->slot(style), style.size, left + 1.5f, baseline, label,
                 theme.textSecondary);
    return;
  }
  if(block.kind != doc::BlockKind::Todo) return;

  const float box = type.body * 0.82f;
  const float boxX = left + 2.0f;
  const float boxY = lineTop + (lineHeight - box) / 2.0f;
  if(block.checked) {
    content.fillRect(boxX, boxY, box, box, theme.accent);
    drawTick(content, boxX, boxY, box, theme.onAccent);
  } else {
    content.strokeRect(boxX, boxY, box, box, 0.7f, theme.textMuted);
  }
  (void)layout;
}

// The ground a block sits on: a code block's fill, a divider's rule, a quote's
// bar, a callout's tint and mark. Clipped to the slice, so a construct that
// crosses a page break wears its chrome on both sides of it.
void paintBlockGround(PdfContent& content, const BlockInk& ink, const doc::DocumentLayout& document,
                      std::size_t index, const BlockSlice& slice, float blockTopOnPage) {
  const doc::SourceBlock& block = document.blocks()[index];
  const doc::BlockLayout& layout = document.layout(index);
  const ui::Theme& theme = exporting::ink();
  const float left = slice.columnLeft + layout.indent;
  const float width = slice.columnWidth - layout.indent;

  if(block.kind == doc::BlockKind::Code) {
    content.fillRect(left, blockTopOnPage + 2.0f, width, std::max(4.0f, layout.height - 6.0f),
                     theme.codeBackground);
    return;
  }
  if(block.kind == doc::BlockKind::Divider) {
    const float middle = blockTopOnPage + layout.height / 2.0f;
    content.line(left, middle, slice.columnLeft + slice.columnWidth, middle, 0.6f, theme.border);
    return;
  }
  if(!slice.ground.inRun) return;
  // Down to where the next block of the run begins rather than to the end of
  // this one, so the pieces tile with no seam between them.
  const float groundHeight = std::max(slice.ground.height, layout.height);
  if(!slice.ground.callout) {
    content.fillRect(left, blockTopOnPage, 2.0f, groundHeight, theme.border);
    return;
  }

  const ui::CalloutStyle style = calloutInk(slice.ground.kind);
  content.fillRect(left, blockTopOnPage, width, groundHeight, style.surface);
  if(!layout.calloutTitle || layout.lines.empty()) return;
  const doc::VisualLine& head = layout.lines.front();
  // The mark is a screen constant, and the page is not the screen: at its own
  // size it is wider than the gutter a quote reserves here and lands on top of
  // the title beside it.
  const float mark = ui::kCalloutMarkSize * kPrintScale;
  const float markY = blockTopOnPage + head.y + (head.height - mark) / 2.0f;
  content.fillRect(left + ui::kCalloutMarkInset * kPrintScale, markY, mark, mark, style.accent);

  // The kind's own name, when the author wrote no title after `[!KIND]`.
  bool titled = false;
  for(const auto& run : layout.runsOf(head)) titled = titled || !run.text.empty();
  if(titled) return;
  doc::RunStyle label;
  label.size = printTypeMetrics().body;
  label.strong = true;
  auto& font = ink.document->font(ink.faces->slot(label));
  const float baseline = blockTopOnPage + head.y + baselineIn(font, head.height, label.size);
  content.text(font, ink.faces->slot(label), label.size, slice.columnLeft + layout.textLeft,
               baseline, ui::calloutLabel(slice.ground.kind), style.accent);
}

}

void paintBlockSlice(PdfContent& content, const BlockInk& ink, const doc::DocumentLayout& document,
                     std::size_t index, const BlockSlice& slice) {
  if(index >= document.blocks().size()) return;
  const doc::SourceBlock& block = document.blocks()[index];
  const doc::BlockLayout& layout = document.layout(index);
  const ui::Theme& theme = exporting::ink();
  // Where this block's own origin would be on the page, which is above the top
  // of the page whenever the slice starts part way down the block.
  const float blockTop = slice.pageY - slice.from;

  content.save();
  // Everything the block paints is confined to the band of page the slice
  // occupies. It is what lets the ground be drawn at the block's full height
  // and still stop at the page edge, rather than each shape being clipped by
  // hand and one of them being got wrong.
  content.clip(slice.columnLeft - 4.0f, slice.pageY, slice.columnWidth + 8.0f,
               slice.to - slice.from);
  paintBlockGround(content, ink, document, index, slice, blockTop);

  for(std::size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
    const doc::VisualLine& line = layout.lines[lineIndex];
    if(line.y + line.height <= slice.from || line.y >= slice.to) continue;
    const float lineTop = blockTop + line.y;

    if(lineIndex == 0) {
      paintListChrome(content, ink, block, layout, slice.columnLeft + layout.indent, lineTop,
                      line.height);
    }
  }

  RunPaint runs;
  runs.x = slice.columnLeft;
  runs.y = blockTop;
  runs.bodyInk = inkFor(doc::TextRole::Body, block.kind);
  // A ticked task is done being read: the layout struck it through, and muting
  // the ink is the other half of saying so.
  if(block.kind == doc::BlockKind::Todo && block.checked) runs.bodyInk = theme.textMuted;
  if(layout.calloutTitle) runs.bodyInk = calloutInk(slice.ground.kind).accent;
  runs.from = slice.from;
  runs.to = slice.to;
  paintRuns(content, ink, layout, runs);

  for(const auto& image : layout.images) {
    if(image.rect.w <= 0.0f || image.rect.h <= 0.0f) continue;
    if(image.rect.y + image.rect.h <= slice.from || image.rect.y >= slice.to) continue;
    if(!ink.pictures) continue;
    const auto& picture = ink.pictures->resolve(image.target);
    if(!picture.drawable()) continue;
    content.image(picture.slot, slice.columnLeft + image.rect.x, blockTop + image.rect.y,
                  image.rect.w, image.rect.h);
  }
  content.restore();
}

float baselineIn(const PdfFont& font, float lineHeight, float size) {
  const float ascent = font.ascent(size);
  const float glyphHeight = ascent - font.descent(size);
  const float leading = std::max(0.0f, lineHeight - glyphHeight);
  return leading / 2.0f + ascent;
}

void paintRuns(PdfContent& content, const BlockInk& ink, const doc::BlockLayout& layout,
               const RunPaint& paint) {
  const ui::Theme& theme = exporting::ink();
  const float defaultSize = printTypeMetrics().body;
  const bool banded = paint.to > paint.from;
  for(const auto& line : layout.lines) {
    if(banded && (line.y + line.height <= paint.from || line.y >= paint.to)) continue;
    const float lineTop = paint.y + line.y;
    // Every tinted ground on this line, before any of its text: a band's
    // sideways inflation reaches into the run beside it, and in a content
    // stream as on screen the later operation is the one that shows. See
    // `ui::forEachCodeSpan`.
    ui::forEachCodeSpan(layout.runsOf(line), [&](float spanX, float spanWidth) {
      content.fillRect(paint.x + spanX - 1.5f, lineTop + 0.5f, spanWidth + 3.0f,
                       line.height - 1.0f, theme.codeBackground);
    });
    for(const auto& run : layout.runsOf(line)) {
      if(run.text.empty()) continue;
      const float size = run.style.size > 0.0f ? run.style.size : defaultSize;
      const int slot = ink.faces->slot(run.style);
      auto& font = ink.document->font(slot);
      const float x = paint.x + run.rect.x;
      const float baseline = lineTop + baselineIn(font, line.height, size);

      const SDL_Color colour =
        run.role == doc::TextRole::Body ? paint.bodyInk : ui::inkFor(theme, run.role);
      content.text(font, slot, size, x, baseline, run.text, colour);

      if(run.style.strike) {
        content.line(x, baseline - size * 0.28f, x + run.rect.w, baseline - size * 0.28f,
                     std::max(0.4f, size * 0.05f), colour);
      }
      if(run.linkIndex < 0 || run.linkIndex >= static_cast<int>(layout.links.size())) continue;
      // No rule under an image's caption: the picture below it is the
      // affordance, and an underline there reads as a stray link.
      if(run.role != doc::TextRole::ImageAlt) {
        const float under = baseline + size * 0.12f;
        content.line(x, under, x + run.rect.w, under, std::max(0.35f, size * 0.045f),
                     theme.accent);
      }
      // And the rect a reader can click, for the targets that mean the same
      // thing on someone else's machine as they do here.
      //
      // `doc::isRemoteTarget` is the whole of that, and deliberately: it is
      // the same question `followLinkAt` asks before handing a target to the
      // desktop, so the page offers exactly the links the reading pane
      // follows. A relative path names a file inside *this* library and a
      // `[[wikilink]]` names a note in it -- neither is anywhere a reader of
      // the PDF can go, and a `file:` URI pointing into somebody else's home
      // directory is worse than no link at all.
      if(ink.links == nullptr) continue;
      const std::string& target = layout.links[static_cast<std::size_t>(run.linkIndex)];
      if(!doc::isRemoteTarget(target)) continue;
      // Adjacent runs of one link on one line make one rect. A `[two words]
      // (...)` label is three runs -- the two words and the space between them
      // -- and three annotations over one phrase is three objects in the file
      // saying the same thing. A label broken across two lines still gets two,
      // which is right: the halves are in different places on the page, and
      // one rect around both would make the margin between them clickable.
      if(!ink.links->empty()) {
        auto& previous = ink.links->back();
        if(previous.uri == target && previous.y == lineTop && previous.height == line.height &&
           std::abs(previous.x + previous.width - x) < 0.01f) {
          previous.width = x + run.rect.w - previous.x;
          continue;
        }
      }
      ink.links->push_back({x, lineTop, run.rect.w, line.height, target});
    }
  }
}

}
