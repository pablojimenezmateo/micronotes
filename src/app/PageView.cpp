#include "app/PageViewStyle.h"

#include "CoreAliases.h"
#include "app/FrameTrace.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/Fold.h"
#include "ui/Fonts.h"
#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"
#include "doc/LinkTarget.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <utility>

namespace micronotes::app {
namespace {

using micronotes::ui::Rect;
using micronotes::ui::TextRenderer;
using micronotes::ui::drawChevron;
using micronotes::ui::fill;
using micronotes::ui::hLine;
using micronotes::ui::stroke;
using micronotes::ui::theme;

using pageview::kContentTopPadding;
using pageview::kGutterWidth;
using pageview::toRect;
using pageview::toTextStyle;

}

doc::TypeMetrics documentTypeMetrics() {
  doc::TypeMetrics metrics;
  metrics.body = ui::type().body;
  metrics.mono = ui::type().mono;
  for(int level = 1; level <= 6; ++level) metrics.heading[level - 1] = ui::headingSize(level);
  metrics.lineHeightRatio = ui::type().lineHeightRatio;
  return metrics;
}

doc::Metrics documentMetrics(ui::TextRenderer& text) {
  doc::Metrics metrics;
  ui::TextRenderer* renderer = &text;
  metrics.measure = [renderer](std::string_view value, const doc::RunStyle& style) {
    return static_cast<float>(renderer->width(value, toTextStyle(style)));
  };
  metrics.lineHeight = [renderer](const doc::RunStyle& style) {
    const float ratio = style.size >= ui::type().h3 ? 1.25f : ui::type().lineHeightRatio;
    const float fromRatio = std::round((style.size > 0.0f ? style.size : ui::type().body) * ratio);
    return std::max(static_cast<float>(renderer->lineHeight(toTextStyle(style))), fromRatio);
  };
  return metrics;
}

void PageView::setHooks(PageViewHooks hooks) {
  hooks_ = std::move(hooks);
  wired_ = true;
}

bool PageView::wired() const {
  return wired_;
}

void PageView::setReadOnly(bool readOnly) {
  readOnly_ = readOnly;
}

// The whole per-frame contract, applied in one place.
//
// The `+ 1` on both the revision and the span is the page's "cannot say" being
// zero: a caller's revision has to be shifted into that space before the
// layout's reuse check can believe it. Done here, once, rather than by each
// surface -- which is where the two copies of the off-by-one used to live.
void PageView::beginFrame(const PageFrame& frame) {
  sourceRevision_ = frame.sourceRevision + 1ull;
  editedSpan_ = frame.editedSpan.shiftedBy(1);
  foldRevision_ = frame.foldRevision;
  foldsActive_ = frame.foldsActive;
  wikiLinkRevision_ = frame.wikiLinkRevision;
  imageRevision_ = frame.imageRevision;
  headerHeight_ = std::max(0.0f, frame.headerHeight);
  pointerX_ = frame.pointerX;
  pointerY_ = frame.pointerY;
  blockSelection_ = frame.blockSelection;
  dropOffset_ = frame.dropOffset;
  selecting_ = frame.selecting;
  caretVisible_ = frame.caretVisible;
}

const doc::DocumentLayout& PageView::document() const {
  return document_;
}

doc::BlockSpan PageView::blocksAt(std::uint64_t sourceRevision) const {
  return document_.blocksAt(sourceRevision);
}

const std::vector<PageLink>& PageView::links() const {
  return links_;
}

ui::Rect PageView::columnRect() const {
  return {columnLeft_, contentTop_, columnWidth_, page_.y + page_.h - contentTop_};
}

Rect PageView::pageRect() const {
  return page_;
}

int PageView::scroll() const {
  return scroll_.scroll();
}

void PageView::setScroll(int value) {
  scroll_.scrollTo(value);
}

int PageView::maxScroll() const {
  return scroll_.maxScroll();
}

void PageView::wheel(float notches, float pixelsPerNotch) {
  scroll_.wheel(notches, pixelsPerNotch);
}

// The header is part of the scroll, not part of the viewport, so it counts as
// content: the first block starts below it and scrolling down takes both away
// together.
void PageView::recordScrollExtent() {
  scroll_.setContent(page_.h - kContentTopPadding * 2.0f, headerHeight_ + document_.totalHeight());
}

void PageView::setRawOffset(std::optional<std::size_t> offset) {
  rawOffset_ = offset;
}

std::optional<std::size_t> PageView::rawOffset() const {
  return rawOffset_;
}

float PageView::originX() const {
  return columnLeft_;
}

float PageView::originY() const {
  return contentTop_ - static_cast<float>(scroll_.scroll());
}

void PageView::layout(TextRenderer& text, std::string_view source, std::size_t caret, Rect rect) {
  const perf::ScopeTimer timer("page.layout");
  rect_ = rect;
  page_ = ui::pageRectIn(rect);
  // The same two functions the reading pane lays itself out with, so the same
  // note has the same measure in both panes. See ui::pageColumnIn.
  const ui::PageColumn column = ui::pageColumnIn(page_, kGutterWidth);
  columnWidth_ = column.width;
  columnLeft_ = column.left;
  // The header is part of the scroll, not part of the viewport: the first block
  // starts below it, and scrolling down takes both away together.
  contentTop_ = page_.y + kContentTopPadding + headerHeight_;

  // Installing metrics drops every cached block layout, so it happens only when
  // the faces actually change, not once a frame...
  // ...and the renderer it measures through, which the metrics hold by pointer.
  // A different one at the same scale would otherwise keep measuring through
  // the old one.
  if(text_ != &text || std::abs(text.displayScale() - metricsScale_) > 0.001f) {
    text_ = &text;
    metricsScale_ = text.displayScale();
    doc::Metrics metrics = documentMetrics(text);
    metrics.measureComplex = [this](const doc::SourceBlock& block, float width) {
      return hooks_.measureComplex ? hooks_.measureComplex(block, width) : 0.0f;
    };
    metrics.measureImage = [this](std::string_view target, float column, float maxHeight) {
      return hooks_.measureImage ? hooks_.measureImage(target, column, maxHeight) : doc::ImageBox {};
    };
    document_.setMetrics(std::move(metrics));
  }

  doc::LayoutOptions options;
  options.width = columnWidth_;
  options.fontScale = text.displayScale();
  options.type = documentTypeMetrics();
  // A read-only page never reveals a block's markers, so it has no caret as far
  // as the layout is concerned: that is the whole of what "reading" means to it.
  options.caretOffset = readOnly_ ? doc::DocumentLayout::kNone : caret;
  options.rawOffset = readOnly_ ? doc::DocumentLayout::kNone
                                : (rawOffset_ ? *rawOffset_ : doc::DocumentLayout::kNone);
  options.folded = foldsActive_ ? folds_.collapsed : nullptr;
  options.wikiLinkResolves = hooks_.wikiLinkResolves;
  options.wikiLinkRevision = wikiLinkRevision_;
  // A picture is fitted to the column and to a share of the page, so a note is
  // not one photograph the reader has to scroll past.
  options.imageMaxHeight = page_.h * 0.55f;
  options.imageRevision = imageRevision_;
  options.sourceRevision = sourceRevision_;
  options.foldRevision = foldRevision_;
  options.editedSpan = editedSpan_;
  document_.update(source, options);

  // The caret must never be stranded inside something collapsed - Ctrl+End, an
  // undone edit or a jump from find can all put it there - so the fold that
  // swallowed it gives way. Nested folds unwind one pass at a time.
  for(int attempt = 0; attempt < 8 && folds_.collapsed && folds_.expand; ++attempt) {
    const auto& blocks = document_.blocks();
    const std::size_t index = doc::blockIndexAt(blocks, std::min(caret, source.size()));
    if(!document_.blockHidden(index)) break;
    bool expanded = false;
    for(std::size_t i = index; i-- > 0;) {
      if(!folds_.collapsed(blocks[i]) || doc::foldEnd(blocks, i) <= index) continue;
      folds_.expand(blocks[i]);
      expanded = true;
      break;
    }
    if(!expanded) break;
    // The expand just moved the fold state, so the stamp the caller handed in
    // no longer describes it. Withdrawing it makes the layout resolve the folds
    // itself for this pass; the caller's next frame carries a moved stamp and
    // the fast path picks up again from there.
    options.foldRevision = 0;
    document_.update(source, options);
  }
  recordScrollExtent();
}

void PageView::setFolds(PageFolds folds) {
  folds_ = std::move(folds);
}

Rect PageView::headerRect() const {
  // Directly above the first block, which is where originY() points. It sits in
  // the scrolling space, so this rect walks off the top of the page as the note
  // is scrolled -- exactly as the first paragraph does.
  return {columnLeft_, originY() - headerHeight_, columnWidth_, headerHeight_};
}

// The anchors an in-note link can land on: every heading, by the slug
// `doc::headingAnchor` makes of its text, and every footnote definition, by its
// own label and by its ordinal. Built from the block partition rather than from
// a second parse of the note -- which is what the reading pane used to do, in a
// walk it made once a frame.
//
// A footnote definition is a `Complex` block as far as the scanner is
// concerned, so its label is read off the front of its own source: `[^label]:`.
void PageView::buildAnchors() const {
  anchors_.clear();
  anchorsValid_ = true;
  anchorRevision_ = sourceRevision_;
  const std::string& source = document_.source();
  const auto& blocks = document_.blocks();
  int footnote = 1;
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    if(block.kind == doc::BlockKind::Heading) {
      std::string_view text(source);
      text = text.substr(block.contentStart(), block.contentEnd() - block.contentStart());
      // A closing run of hashes is decoration in an ATX heading, not title.
      while(!text.empty() && (text.back() == '#' || text.back() == ' ')) text.remove_suffix(1);
      auto anchor = doc::headingAnchor(text);
      if(anchor.empty()) continue;
      anchors_.emplace(std::move(anchor), document_.blockTop(i));
      continue;
    }
    if(block.kind != doc::BlockKind::Complex) continue;
    std::string_view body(source);
    body = body.substr(block.start, block.end() - block.start);
    if(body.size() < 4 || body[0] != '[' || body[1] != '^') continue;
    const auto close = body.find("]:");
    if(close == std::string_view::npos) continue;
    const auto label = body.substr(2, close - 2);
    if(label.empty()) continue;
    const float top = document_.blockTop(i);
    anchors_.emplace("fn-" + std::string(label), top);
    anchors_.emplace("fn-" + std::to_string(footnote++), top);
  }
}

std::optional<int> PageView::anchorScroll(std::string_view anchor) const {
  if(!anchorsValid_ || sourceRevision_ == 0 || anchorRevision_ != sourceRevision_) buildAnchors();
  const auto found = anchors_.find(anchor);
  if(found == anchors_.end()) return std::nullopt;
  // The header is part of the scrolling space above the first block, so a jump
  // has to clear it as well as the blocks above the target.
  return std::max(0, static_cast<int>(std::lround(headerHeight_ + found->second)));
}

void PageView::revealCaret(std::size_t offset) {
  const auto caret = document_.caretRect(offset);
  // In scroll space rather than document space: the scroll counts from the top
  // of the header, so a caret in the first block is `headerHeight_` further
  // down than its document coordinate says.
  const float top = caret.y + headerHeight_;
  scroll_.reveal(top, top + caret.h, std::max(1.0f, page_.h - kContentTopPadding * 2.0f));
}

std::size_t PageView::offsetAt(float x, float y) const {
  return document_.offsetAt(x - originX(), y - originY());
}

std::optional<std::size_t> PageView::blockAt(float x, float y) const {
  (void)x;
  return document_.blockAt(y - originY());
}

std::string PageView::linkAt(float x, float y) const {
  for(const auto& link : links_) {
    if(ui::contains(link.rect, x, y)) return link.target;
  }
  return {};
}

std::optional<std::size_t> PageView::checkboxAt(float x, float y) const {
  for(const auto& box : checkboxes_) {
    if(ui::contains(box.rect, x, y)) return box.blockStart;
  }
  return std::nullopt;
}

std::optional<PageGutterHit> PageView::gutterAt(float x, float y) const {
  for(const auto& hit : gutter_) {
    if(ui::contains(hit.rect, x, y)) return hit;
  }
  return std::nullopt;
}

std::optional<PageFoldHit> PageView::foldAt(float x, float y) const {
  for(const auto& hit : foldHits_) {
    if(ui::contains(hit.rect, x, y)) return hit;
  }
  return std::nullopt;
}

std::optional<std::size_t> PageView::copyButtonAt(float x, float y) const {
  for(const auto& button : codeButtons_) {
    if(ui::contains(button.rect, x, y)) return button.blockStart;
  }
  return std::nullopt;
}

std::string PageView::toolbarAt(float x, float y) const {
  for(const auto& button : toolbar_) {
    if(ui::contains(button.rect, x, y)) return button.id;
  }
  return {};
}

std::size_t PageView::dropOffsetAt(float y) const {
  const auto& blocks = document_.blocks();
  const float docY = y - originY();
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    // A block inside a collapsed fold has no height and no place on screen, so
    // it is not somewhere the pointer can mean to drop anything.
    if(document_.layout(i).hidden) continue;
    const float top = document_.blockTop(i);
    if(docY < top + document_.layout(i).height / 2.0f) return blocks[i].start;
  }
  return document_.source().size();
}

// The blocks whose boxes reach the viewport, widened backwards to the head of a
// quote or callout run the first visible block sits inside.
//
// The run matters because its container -- the tinted box, the bar down the left
// -- is drawn once, by the block that starts it. A run that begins above the
// viewport and continues into it would otherwise lose its box the moment its
// first line scrolled off, which is a visible bug and not a subtle one.
std::pair<std::size_t, std::size_t> PageView::visibleBlocks() const {
  const float oy = originY();
  auto range = document_.blockRange(page_.y - oy, page_.y + page_.h - oy);
  const auto& blocks = document_.blocks();
  while(range.first > 0 && range.first < blocks.size() &&
        !doc::startsQuoteRun(blocks, range.first)) {
    const doc::BlockKind kind = blocks[range.first].kind;
    if(kind != doc::BlockKind::Quote && kind != doc::BlockKind::Callout) break;
    --range.first;
  }
  return range;
}

ui::Rect PageView::blockRect(std::size_t index) const {
  const float top = originY() + document_.blockTop(index);
  return {columnLeft_, top, columnWidth_, document_.layout(index).height};
}

std::size_t PageView::rowRelative(std::size_t offset, int deltaRows) const {
  return document_.rowRelative(offset, deltaRows);
}

std::size_t PageView::rowsPerPage() const {
  return document_.rowsPerHeight(std::max(1.0f, page_.h - kContentTopPadding * 2.0f));
}

}
