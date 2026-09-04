#include "app/PageView.h"

#include "CoreAliases.h"
#include "app/FrameTrace.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/Fold.h"
#include "ui/Fonts.h"
#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <utility>

namespace micronotes::app {
namespace {

using micronotes::ui::Rect;
using micronotes::ui::TextRenderer;
using micronotes::ui::drawDisclosure;
using micronotes::ui::fill;
using micronotes::ui::fillRounded;
using micronotes::ui::hLine;
using micronotes::ui::stroke;
using micronotes::ui::theme;

constexpr float kContentTopPadding = 18.0f;
// Reserved to the left of the content column for the hover handles. The
// column is centred when the page is wide enough and pushed right when it is
// not, so the affordances always have somewhere to live.
// Three affordances live here, left to right: insert, drag handle, and the
// disclosure control, which sits closest to the text because it belongs to the
// block rather than to the pointer.
constexpr float kGutterWidth = 78.0f;
constexpr float kInsertOffset = 70.0f;
constexpr float kHandleOffset = 48.0f;
constexpr float kFoldOffset = 22.0f;

ui::TextStyle toTextStyle(const doc::RunStyle& style) {
  ui::TextStyle out;
  out.family = style.mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  out.strong = style.strong;
  out.italic = style.italic;
  out.size = style.size;
  return out;
}

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

namespace {

// Hand-drawn: the vendored UI face carries no disclosure glyph, and a missing
// glyph in the gutter would read as a rendering bug.
SDL_Color colorFor(doc::TextRole role) {
  switch(role) {
    case doc::TextRole::Marker: return theme().dim;
    case doc::TextRole::Link:
    case doc::TextRole::WikiLink: return theme().accent;
    case doc::TextRole::WikiLinkUnresolved: return theme().linkPending;
    case doc::TextRole::ImageAlt: return theme().muted;
    case doc::TextRole::Muted: return theme().muted;
    case doc::TextRole::Code: return theme().text;
    case doc::TextRole::Body: break;
  }
  return theme().text;
}

// A quote reads as someone else's words, so its body sits a step back from the
// page's own text. Markers and links keep their own roles.
SDL_Color colorFor(doc::TextRole role, doc::BlockKind kind) {
  const bool quoted = kind == doc::BlockKind::Quote || kind == doc::BlockKind::Callout;
  if(quoted && role == doc::TextRole::Body) return theme().muted;
  return colorFor(role);
}

Rect toRect(const doc::Rect& rect, float originX, float originY) {
  return {rect.x + originX, rect.y + originY, rect.w, rect.h};
}

}

void PageView::setRevisions(std::uint64_t source, std::uint64_t folds) {
  sourceRevision_ = source;
  foldRevision_ = folds;
}

void PageView::setHooks(PageViewHooks hooks) {
  hooks_ = std::move(hooks);
  wired_ = true;
}

bool PageView::wired() const {
  return wired_;
}

void PageView::setWikiLinkRevision(std::uint64_t revision) {
  wikiLinkRevision_ = revision;
}

void PageView::setImageRevision(std::uint64_t revision) {
  imageRevision_ = revision;
}

void PageView::setReadOnly(bool readOnly) {
  readOnly_ = readOnly;
}

void PageView::setFoldsActive(bool active) {
  foldsActive_ = active;
}

void PageView::setEditedSpan(doc::LayoutOptions::EditedSpan span) {
  editedSpan_ = span;
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
  return scroll_;
}

void PageView::setScroll(int value) {
  scroll_ = std::clamp(value, 0, maxScroll());
}

int PageView::maxScroll() const {
  const float visible = std::max(1.0f, page_.h - kContentTopPadding * 2.0f);
  return std::max(0, static_cast<int>(std::ceil(headerHeight_ + document_.totalHeight() - visible)));
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
  return contentTop_ - static_cast<float>(scroll_);
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
  scroll_ = std::clamp(scroll_, 0, maxScroll());
}

void PageView::setFolds(PageFolds folds) {
  folds_ = std::move(folds);
}

void PageView::setHeaderHeight(float height) {
  headerHeight_ = std::max(0.0f, height);
}

Rect PageView::headerRect() const {
  // Directly above the first block, which is where originY() points. It sits in
  // the scrolling space, so this rect walks off the top of the page as the note
  // is scrolled -- exactly as the first paragraph does.
  return {columnLeft_, originY() - headerHeight_, columnWidth_, headerHeight_};
}

// The anchors an in-note link can land on: every heading, by the slug
// `ui::headingAnchor` makes of its text, and every footnote definition, by its
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
      auto anchor = ui::headingAnchor(text);
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
  const float visible = std::max(1.0f, page_.h - kContentTopPadding * 2.0f);
  // In scroll space rather than document space: the scroll counts from the top
  // of the header, so a caret in the first block is `headerHeight_` further
  // down than its document coordinate says.
  const float top = caret.y + headerHeight_;
  if(top < static_cast<float>(scroll_)) {
    scroll_ = static_cast<int>(std::floor(top));
  } else if(top + caret.h > static_cast<float>(scroll_) + visible) {
    scroll_ = static_cast<int>(std::ceil(top + caret.h - visible));
  }
  scroll_ = std::clamp(scroll_, 0, maxScroll());
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

void PageView::setPointer(float x, float y) {
  pointerX_ = x;
  pointerY_ = y;
}

void PageView::setBlockSelection(PageBlockSelection selection) {
  blockSelection_ = selection;
}

void PageView::setDropOffset(std::optional<std::size_t> offset) {
  dropOffset_ = offset;
}

void PageView::setSelecting(bool selecting) {
  selecting_ = selecting;
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

void PageView::draw(SDL_Renderer* renderer, TextRenderer& text, std::size_t caret, const PageSelection& selection,
                    bool focused, std::string_view findQuery) {
  const perf::ScopeTimer timer("page.draw");
  perf::addCounter(perf::CounterId::PageDrawCalls);
  links_.clear();
  checkboxes_.clear();
  toolbar_.clear();
  gutter_.clear();
  foldHits_.clear();
  codeButtons_.clear();
  fill(renderer, rect_, theme().editorBg);
  // Flat, with no outline. The page used to be drawn as a bordered card that
  // turned accent-coloured when focused, which made the entire writing surface
  // read as a selected text field -- and the border was doing all the work,
  // because pageSurface and the pane behind it are the same colour.
  ui::fill(renderer, page_, theme().pageSurface);

  const float ox = originX();
  const float oy = originY();
  const float viewTop = page_.y;
  const float viewBottom = page_.y + page_.h;
  // Everything below is clipped to the page, and the guard scope ends before
  // the gutter, the fold controls and the toolbar, which draw beside it.
  {
  const ui::ClipGuard pageClip(renderer, {page_.x + 1.0f, page_.y + 1.0f, page_.w - 2.0f, page_.h - 2.0f});

  if(blockSelection_.active) {
    // Whole blocks, highlighted edge to edge: a block selection is an object
    // selection, and should not read as a run of selected text.
    const auto& blocks = document_.blocks();
    std::size_t first = doc::blockIndexAt(blocks, blockSelection_.anchor);
    std::size_t last = doc::blockIndexAt(blocks, blockSelection_.focus);
    if(last < first) std::swap(first, last);
    for(std::size_t i = first; i <= last && i < blocks.size(); ++i) {
      const Rect band = blockRect(i);
      fill(renderer, {band.x - 6.0f, band.y, band.w + 12.0f, std::max(2.0f, band.h)}, theme().selectionBg);
    }
  } else if(selection.start != selection.end) {
    // Banded to the viewport, in document space. A selection reaching the whole
    // note is otherwise a pass over every visual row in it, per frame, to paint
    // the forty the window can show.
    const float bandTop = page_.y - oy;
    document_.selectionRectsInto(selection.start, selection.end, bandTop, bandTop + page_.h,
                                 &selectionRects_);
    for(const auto& rect : selectionRects_) {
      fill(renderer, toRect(rect, ox, oy), theme().selectionBg);
    }
  }
  const auto& blocks = document_.blocks();
  // Only the blocks that reach the viewport. This used to walk the whole note
  // and test each block against the page, which made a draw cost the document
  // rather than the window: 10,801 blocks visited to draw 18 of them, every
  // frame, on a 235 KB note. The counters below are what said so, and they stay
  // to keep saying so.
  const auto [firstBlock, lastBlock] = visibleBlocks();

  if(!findQuery.empty()) {
    const perf::ScopeTimer findTimer("page.draw.find_highlight");
    drawFindHighlights(renderer, findQuery, firstBlock, lastBlock, ox, oy);
  } else {
    findMatchesValid_ = false;
  }

  drawBlockDecorations(renderer, text);

  perf::addCounter(perf::CounterId::PageBlocksVisited, lastBlock - firstBlock);
  std::size_t blocksDrawn = 0;
  std::size_t runsDrawn = 0;
  for(std::size_t i = firstBlock; i < lastBlock; ++i) {
    const doc::SourceBlock& block = blocks[i];
    const doc::BlockLayout& layout = document_.layout(i);
    const float top = oy + document_.blockTop(i);
    if(top + layout.height < viewTop || top > viewBottom) continue;
    ++blocksDrawn;

    const float left = ox + layout.indent;
    const float bodyLine = layout.lines.empty() ? 0.0f : layout.lines.front().height;

    // List chrome stands in for the marker text while the marker is hidden.
    if(!layout.revealed && !layout.raw && bodyLine > 0.0f) {
      const float markerY = top + (layout.lines.empty() ? 0.0f : layout.lines.front().y);
      if(block.kind == doc::BlockKind::Bullet) {
        ui::TextStyle style;
        style.size = ui::type().body;
        text.draw("•", left + 6.0f, markerY, theme().muted, style);
      } else if(block.kind == doc::BlockKind::Ordered) {
        ui::TextStyle style;
        style.size = ui::type().body;
        const auto label = std::to_string(block.ordinal > 0 ? block.ordinal : 1) + ".";
        text.draw(label, left + 2.0f, markerY, theme().muted, style);
      } else if(block.kind == doc::BlockKind::Todo) {
        Rect box {left + 3.0f, markerY + 4.0f, 14.0f, 14.0f};
        // A generous hit area: the drawn box is deliberately small.
        const Rect hit {box.x - 4.0f, box.y - 4.0f, box.w + 8.0f, box.h + 8.0f};
        checkboxes_.push_back({hit, block.start});
        const bool hot = ui::contains(hit, pointerX_, pointerY_);
        if(block.checked) {
          fillRounded(renderer, box, theme().accent, ui::kRadiusSmall);
          const SDL_Color tick = theme().onAccent;
          SDL_SetRenderDrawColor(renderer, tick.r, tick.g, tick.b, tick.a);
          SDL_RenderLine(renderer, box.x + 3.5f, box.y + 7.0f, box.x + 6.0f, box.y + 9.5f);
          SDL_RenderLine(renderer, box.x + 6.0f, box.y + 9.5f, box.x + 10.5f, box.y + 4.5f);
        } else {
          // The box lights up under the pointer, because a control that never
          // reacts is one people do not learn is clickable.
          ui::strokeRounded(renderer, box, hot ? theme().accent : theme().dim, ui::kRadiusSmall);
        }
      }
    }

    if(layout.complex) {
      Rect complexRect {ox, top, columnWidth_, layout.height};
      if(hooks_.drawComplex) hooks_.drawComplex(block, complexRect);
      continue;
    }

    // A code block and a block dropped to raw text are both kept inside the
    // column: a long line scrolls off its own right edge rather than out over
    // the gutter. `std::optional` because the guard is the scope, and this one
    // has to end with the block rather than with the loop body it lives in.
    std::optional<ui::ClipGuard> columnClip;
    if(block.kind == doc::BlockKind::Code || layout.raw) {
      columnClip.emplace(renderer, Rect {ox, std::max(page_.y + 1.0f, top), columnWidth_,
                                         std::min(layout.height, page_.y + page_.h - top)});
    }
    for(const auto& line : layout.lines) {
      const float lineY = top + line.y;
      if(lineY + line.height < viewTop || lineY > viewBottom) continue;
      for(const auto& run : layout.runsOf(line)) {
        if(run.text.empty()) continue;
        ++runsDrawn;
        const ui::TextStyle style = toTextStyle(run.style);
        const float x = ox + run.rect.x;
        if(run.role == doc::TextRole::Code && !run.isMarker) {
          fill(renderer, {x - 2.0f, lineY + 1.0f, run.rect.w + 4.0f, line.height - 2.0f}, theme().codeBg);
        }
        SDL_Color ink = colorFor(run.role, block.kind);
        // A ticked task is done being read. The layout already struck it
        // through; muting the ink is the other half of saying so.
        if(block.kind == doc::BlockKind::Todo && block.checked && !layout.revealed &&
           run.role == doc::TextRole::Body) {
          ink = theme().dim;
        }
        // A callout's head line is its name, so it is drawn in the kind's own
        // colour rather than in the muted ink the rest of a quote takes.
        if(layout.calloutTitle && run.role == doc::TextRole::Body) {
          ink = ui::calloutStyle(block.info(document_.source())).accent;
        }
        text.draw(run.text, x, lineY, ink, style);
        if(run.style.strike) {
          hLine(renderer, x, x + run.rect.w, lineY + line.height * 0.45f, ink);
        }
        if(run.linkIndex >= 0 && run.linkIndex < static_cast<int>(layout.links.size())) {
          // No rule under an image's caption: the picture below it is the
          // affordance, and an underline there reads as a stray link.
          if(run.role != doc::TextRole::ImageAlt) {
            hLine(renderer, x, x + run.rect.w, lineY + line.height - 4.0f, theme().accentDim);
          }
          const bool wiki = run.role == doc::TextRole::WikiLink ||
                            run.role == doc::TextRole::WikiLinkUnresolved;
          links_.push_back({{x, lineY, run.rect.w, line.height},
                            layout.links[static_cast<std::size_t>(run.linkIndex)], wiki});
        }
      }
    }

    // The pictures under the block. Their boxes were reserved by the layout, so
    // this is a blit at a rect that is already right rather than a second
    // measure of the same file.
    for(const auto& image : layout.images) {
      if(image.rect.w <= 0.0f || image.rect.h <= 0.0f) continue;
      const Rect box {ox + image.rect.x, top + image.rect.y, image.rect.w, image.rect.h};
      if(box.y + box.h < viewTop || box.y > viewBottom) continue;
      if(hooks_.drawImage) hooks_.drawImage(image.target, box);
      links_.push_back({box, image.target, false});
    }
  }
  perf::addCounter(perf::CounterId::PageBlocksDrawn, blocksDrawn);
  perf::addCounter(perf::CounterId::PageRunsDrawn, runsDrawn);
  if(ScopedFrame* frame = ScopedFrame::current()) {
    frame->addBlocks(lastBlock - firstBlock, blocksDrawn, document_.lastRelaidBlocks());
    frame->addRuns(runsDrawn);
  }

  if(focused && !readOnly_ && !blockSelection_.active) {
    const auto rect = document_.caretRect(caret);
    const Rect caretRect = toRect(rect, ox, oy);
    if(caretRect.y + caretRect.h >= viewTop && caretRect.y <= viewBottom) {
      fill(renderer, {caretRect.x, caretRect.y + 1.0f, 2.0f, std::max(4.0f, caretRect.h - 2.0f)}, theme().accent);
    }
  }
  drawCodeChrome(renderer, text);
  if(!readOnly_) drawDropIndicator(renderer);
  }
  drawFoldControls(renderer);
  // The three things an editable surface adds. Nothing else about the two
  // surfaces differs, which is why the reading pane is this page rather than a
  // second renderer for the same Markdown.
  if(!readOnly_) {
    drawGutter(renderer, text);
    drawToolbar(renderer, text, selection);
  }
  ui::drawVerticalScrollbar(renderer, page_, scroll_, maxScroll());
}

// Highlighting a find query used to be two O(document) costs on every frame the
// find bar was open: `std::string::find` over the whole note, and a
// `selectionRects` call -- which measures text -- for every match in the file,
// however far off screen it was. On a 235 KB note with a common word in it that
// is the whole frame.
//
// Both are bounded here. The match list is a function of the buffer and the
// query, so it is found once and stands until one of them moves; and only the
// matches inside the band of blocks the window is showing get a rect built for
// them, which is a pair of binary searches over a sorted list.
void PageView::drawFindHighlights(SDL_Renderer* renderer, std::string_view findQuery,
                                  std::size_t firstBlock, std::size_t lastBlock, float ox, float oy) {
  const std::string& source = document_.source();
  const std::size_t step = std::max<std::size_t>(1, findQuery.size());
  // A caller with no revision to offer -- a test, or a surface that does not
  // stamp its buffer -- gets the search every frame, exactly as `update` does.
  const bool cached = findMatchesValid_ && sourceRevision_ != 0 &&
                      findMatchRevision_ == sourceRevision_ && findMatchQuery_ == findQuery;
  if(!cached) {
    perf::addCounter(perf::CounterId::PageFindScanBytes, source.size());
    findMatches_.clear();
    for(std::size_t at = source.find(findQuery); at != std::string::npos;
        at = source.find(findQuery, at + step)) {
      findMatches_.push_back(at);
    }
    findMatchQuery_ = findQuery;
    findMatchRevision_ = sourceRevision_;
    findMatchesValid_ = true;
  }
  if(findMatches_.empty()) return;

  const auto& blocks = document_.blocks();
  if(firstBlock >= lastBlock || blocks.empty()) return;
  // A match starting just before the first visible block can still reach into
  // it, so the band opens one block early. It cannot reach further than that:
  // a block boundary is a line boundary and a match is one line of source.
  const std::size_t from = blocks[firstBlock > 0 ? firstBlock - 1 : 0].start;
  const std::size_t to = blocks[std::min(lastBlock, blocks.size()) - 1].end();
  const auto begin = std::lower_bound(findMatches_.begin(), findMatches_.end(), from);
  const auto end = std::lower_bound(findMatches_.begin(), findMatches_.end(), to);
  perf::addCounter(perf::CounterId::PageFindHighlightsDrawn,
                   static_cast<std::uint64_t>(end - begin));
  const float bandTop = page_.y - oy;
  for(auto it = begin; it != end; ++it) {
    document_.selectionRectsInto(*it, *it + findQuery.size(), bandTop, bandTop + page_.h,
                                 &selectionRects_);
    for(const auto& rect : selectionRects_) {
      const Rect hit = toRect(rect, ox, oy);
      fill(renderer, hit, theme().findBg);
      stroke(renderer, hit, theme().findBorder);
    }
  }
}

void PageView::drawBlockDecorations(SDL_Renderer* renderer, TextRenderer& text) {
  const float ox = originX();
  const float oy = originY();
  const float viewTop = page_.y;
  const float viewBottom = page_.y + page_.h;
  const auto& blocks = document_.blocks();
  const auto [firstBlock, lastBlock] = visibleBlocks();
  perf::addCounter(perf::CounterId::PageDecorationBlocksVisited, lastBlock - firstBlock);

  for(std::size_t i = firstBlock; i < lastBlock; ++i) {
    const doc::SourceBlock& block = blocks[i];
    const doc::BlockLayout& layout = document_.layout(i);
    if(layout.hidden) continue;
    const float top = oy + document_.blockTop(i);
    const float left = ox + layout.indent;

    if(block.kind == doc::BlockKind::Code) {
      if(layout.complex || top + layout.height < viewTop || top > viewBottom) continue;
      const Rect codeRect {left, top + 2.0f, columnWidth_ - layout.indent, std::max(8.0f, layout.height - 8.0f)};
      fillRounded(renderer, codeRect, theme().codeBg, ui::kRadiusSmall);
      continue;
    }

    if(block.kind == doc::BlockKind::Divider) {
      if(top + layout.height < viewTop || top > viewBottom) continue;
      // One rule, centred in the air the layout reserves for it.
      const float middle = std::round(top + layout.height / 2.0f);
      hLine(renderer, left, ox + columnWidth_, middle, theme().divider);
      continue;
    }

    // A run of `>` lines is one quote or one callout: the container is drawn
    // once, over the whole run, rather than once per line with seams between.
    if(!doc::startsQuoteRun(blocks, i)) continue;
    std::size_t last = i;
    while(!doc::endsQuoteRun(blocks, last) && last + 1 < blocks.size()) ++last;
    const float bottom = oy + document_.blockTop(last) + document_.layout(last).height;
    if(bottom < viewTop || top > viewBottom) continue;
    const float height = std::max(8.0f, bottom - top - 2.0f);

    if(block.kind == doc::BlockKind::Quote) {
      fill(renderer, {left, top + 2.0f, 3.0f, height}, theme().divider);
      continue;
    }

    const ui::CalloutStyle style = ui::calloutStyle(block.info(document_.source()));
    const Rect callout {left, top, columnWidth_ - layout.indent, height + 2.0f};
    // No rule down the left edge. The tint is already the whole shape, and a
    // bar beside it makes the box read as a quote wearing a colour rather than
    // as a callout.
    ui::drawRoundedSurface(renderer, callout, style.surface, style.surface, ui::kRadiusMedium);
    if(!layout.calloutTitle) continue;

    // The head line is the title: a mark in the gutter, and the kind's own name
    // when the author did not write one after `[!KIND]`.
    const float lineY = callout.y + (layout.lines.empty() ? 8.0f : layout.lines.front().y);
    const float lineH = layout.lines.empty() ? 20.0f : layout.lines.front().height;
    // Sized and placed to sit inside the quote gutter the layout already
    // reserves, so the mark never crowds the title beside it. The size and the
    // inset are `ui::Metrics`' because the reading pane draws the same mark.
    fillRounded(renderer,
                {std::round(callout.x + ui::kCalloutMarkInset),
                 std::round(lineY + (lineH - ui::kCalloutMarkSize) / 2.0f),
                 ui::kCalloutMarkSize, ui::kCalloutMarkSize},
                style.accent, ui::kCalloutMarkSize / 2.0f);

    bool titled = false;
    if(!layout.lines.empty()) {
      for(const auto& run : layout.runsOf(layout.lines.front())) titled = titled || !run.text.empty();
    }
    if(titled) continue;
    ui::TextStyle label;
    label.size = ui::type().body;
    label.strong = true;
    text.draw(ui::calloutLabel(block.info(document_.source())), ox + layout.textLeft, lineY,
              style.accent, label);
  }
}

void PageView::drawCodeChrome(SDL_Renderer* renderer, TextRenderer& text) {
  const float ox = originX();
  const float oy = originY();
  const auto& blocks = document_.blocks();
  const auto [firstBlock, lastBlock] = visibleBlocks();
  perf::addCounter(perf::CounterId::PageCodeChromeBlocksVisited, lastBlock - firstBlock);
  ui::TextStyle label;
  label.size = ui::type().tiny;

  for(std::size_t i = firstBlock; i < lastBlock; ++i) {
    const doc::SourceBlock& block = blocks[i];
    if(block.kind != doc::BlockKind::Code) continue;
    const doc::BlockLayout& layout = document_.layout(i);
    if(layout.hidden || layout.complex || layout.raw) continue;
    const float top = oy + document_.blockTop(i);
    if(top + layout.height < page_.y || top > page_.y + page_.h) continue;

    const float right = ox + columnWidth_ - 6.0f;
    const float y = top + 5.0f;
    const std::string copy = "Copy";
    const Rect button {right - static_cast<float>(text.width(copy, label)) - 14.0f, y, 
                       static_cast<float>(text.width(copy, label)) + 14.0f, 19.0f};
    // The button is only there while the pointer is on the block it copies. A
    // control sitting on every code block in a long note is a column of
    // "Copy" down the page saying nothing about the code beside it.
    const Rect blockRect {ox, top, columnWidth_, layout.height};
    if(ui::contains(blockRect, pointerX_, pointerY_)) {
      codeButtons_.push_back({button, block.start});
      const bool hot = ui::contains(button, pointerX_, pointerY_);
      // Drawn over the code, so it needs its own ground to stay readable.
      ui::drawRoundedSurface(renderer, button, hot ? theme().surfaceElevated : theme().surface,
                             hot ? theme().hairline : theme().surface, ui::kRadiusSmall);
      text.draw(copy, button.x + 7.0f, button.y + 3.0f, hot ? theme().text : theme().muted, label);
    }

    if(!block.hasInfo()) continue;
    const std::string_view info = block.info(document_.source());
    text.draw(info, button.x - static_cast<float>(text.width(info, label)) - 10.0f, y + 3.0f,
              theme().dim, label);
  }
}

void PageView::drawFoldControls(SDL_Renderer* renderer) {
  const float oy = originY();
  const auto& blocks = document_.blocks();
  const auto [firstBlock, lastBlock] = visibleBlocks();
  perf::addCounter(perf::CounterId::PageFoldControlBlocksVisited, lastBlock - firstBlock);
  const auto hovered = document_.blockAt(pointerY_ - oy);
  const bool onPage = pointerX_ >= page_.x && pointerX_ <= page_.x + page_.w &&
                      pointerY_ >= page_.y && pointerY_ <= page_.y + page_.h;

  for(std::size_t i = firstBlock; i < lastBlock; ++i) {
    const doc::BlockLayout& layout = document_.layout(i);
    if(layout.hidden) continue;
    const float top = oy + document_.blockTop(i);
    if(top + layout.height < page_.y || top > page_.y + page_.h) continue;
    const bool folded = folds_.collapsed && folds_.collapsed(blocks[i]);
    // A collapsed toggle always shows its control - it is the only sign that
    // anything is hidden at all. An expanded one waits to be hovered.
    if(!folded && !(onPage && hovered && *hovered == i)) continue;
    if(!doc::foldable(blocks, i)) continue;

    const float firstLine = layout.lines.empty() ? 0.0f : layout.lines.front().y;
    const float lineHeight = layout.lines.empty() ? 20.0f : layout.lines.front().height;
    // Beside the block it belongs to, so a nested item's control sits with the
    // item rather than out at the page margin.
    const Rect box {columnLeft_ + layout.indent - kFoldOffset,
                    std::round(top + firstLine + (lineHeight - 18.0f) / 2.0f), 18.0f, 18.0f};
    if(box.y + box.h < page_.y || box.y > page_.y + page_.h) continue;
    foldHits_.push_back({box, i, blocks[i].start, folded});
    const bool hot = ui::contains(box, pointerX_, pointerY_);
    if(hot) ui::drawSurface(renderer, box, theme().surface, theme().hairline);
    drawDisclosure(renderer, box, !folded, hot ? theme().text : (folded ? theme().muted : theme().dim));
  }
}

void PageView::drawGutter(SDL_Renderer* renderer, TextRenderer& text) {
  (void)text;
  if(pointerX_ < page_.x || pointerX_ > page_.x + page_.w) return;
  if(pointerY_ < page_.y || pointerY_ > page_.y + page_.h) return;
  const auto index = document_.blockAt(pointerY_ - originY());
  if(!index) return;
  const auto& blocks = document_.blocks();
  if(*index >= blocks.size() || blocks[*index].kind == doc::BlockKind::Blank) return;

  const doc::BlockLayout& layout = document_.layout(*index);
  const float top = originY() + document_.blockTop(*index);
  const float firstLine = layout.lines.empty() ? 0.0f : layout.lines.front().y;
  const float lineHeight = layout.lines.empty() ? 20.0f : layout.lines.front().height;
  const float y = std::round(top + firstLine + (lineHeight - 18.0f) / 2.0f);
  if(y + 18.0f < page_.y || y > page_.y + page_.h) return;

  const Rect insertRect {columnLeft_ - kInsertOffset, y, 18.0f, 18.0f};
  const Rect handleRect {columnLeft_ - kHandleOffset, y, 16.0f, 18.0f};
  gutter_.push_back({insertRect, *index, blocks[*index].start, true});
  gutter_.push_back({handleRect, *index, blocks[*index].start, false});

  const bool overInsert = ui::contains(insertRect, pointerX_, pointerY_);
  const bool overHandle = ui::contains(handleRect, pointerX_, pointerY_);
  if(overInsert) ui::drawSurface(renderer, insertRect, theme().surface, theme().hairline);
  if(overHandle) ui::drawSurface(renderer, handleRect, theme().surface, theme().hairline);

  // Drawn, not typeset: the vendored UI face has no glyph for either mark, and
  // a missing glyph in the gutter would read as a rendering bug.
  const SDL_Color ink = overInsert || overHandle ? theme().text : theme().dim;
  SDL_SetRenderDrawColor(renderer, ink.r, ink.g, ink.b, ink.a);
  const float cx = insertRect.x + insertRect.w / 2.0f;
  const float cy = insertRect.y + insertRect.h / 2.0f;
  SDL_RenderLine(renderer, cx - 4.0f, cy, cx + 4.0f, cy);
  SDL_RenderLine(renderer, cx, cy - 4.0f, cx, cy + 4.0f);
  for(int row = 0; row < 3; ++row) {
    for(int column = 0; column < 2; ++column) {
      fill(renderer, {handleRect.x + 4.0f + static_cast<float>(column) * 5.0f,
                      handleRect.y + 4.0f + static_cast<float>(row) * 4.0f, 2.0f, 2.0f}, ink);
    }
  }
}

void PageView::drawDropIndicator(SDL_Renderer* renderer) {
  if(!dropOffset_) return;
  const auto& blocks = document_.blocks();
  const std::size_t offset = std::min(*dropOffset_, document_.source().size());
  float y = originY();
  if(offset >= document_.source().size() && !blocks.empty()) {
    y += document_.blockTop(blocks.size() - 1) + document_.layout(blocks.size() - 1).height;
  } else {
    y += document_.blockTop(doc::blockIndexAt(blocks, offset));
  }
  y = std::round(y) - 1.0f;
  if(y < page_.y || y > page_.y + page_.h) return;
  fill(renderer, {columnLeft_ - 6.0f, y, columnWidth_ + 12.0f, 2.0f}, theme().accent);
}

void PageView::drawToolbar(SDL_Renderer* renderer, TextRenderer& text, const PageSelection& selection) {
  if(selecting_ || blockSelection_.active) return;
  if(selection.start == selection.end) return;
  // Only the two rects this actually places against, not every rect in the
  // selection: the toolbar reads `front()` to sit above the selection's first
  // row and `back()` to fall back to below its last, and it used to get them by
  // building all 6,600 of a select-all's rows and throwing 6,598 away.
  const auto ends = document_.selectionEnds(selection.start, selection.end);
  if(!ends) return;

  struct Entry { const char* id; const char* label; };
  static constexpr std::size_t kButtons = 6;
  static constexpr Entry entries[kButtons] = {
    {"bold", "B"}, {"italic", "I"}, {"code", "</>"}, {"strike", "S"},
    {"link", "Link"}, {"turn", "Turn into"},
  };
  ui::TextStyle style;
  style.size = ui::type().small;

  const float padding = 9.0f;
  float width = 6.0f;
  float widths[kButtons] = {};
  for(std::size_t i = 0; i < kButtons; ++i) {
    widths[i] = static_cast<float>(text.width(entries[i].label, style)) + padding * 2.0f;
    width += widths[i];
  }
  const float height = 30.0f;

  const Rect first = toRect(ends->first, originX(), originY());
  float x = std::clamp(first.x - 8.0f, page_.x + 6.0f, page_.x + page_.w - width - 6.0f);
  float y = first.y - height - 8.0f;
  if(y < page_.y + 4.0f) {
    // No room above: sit below the selection rather than off the page.
    const Rect last = toRect(ends->second, originX(), originY());
    y = last.y + last.h + 8.0f;
  }
  if(y + height > page_.y + page_.h - 4.0f) return;

  ui::drawSurface(renderer, {x, y, width, height}, theme().surfaceElevated, theme().hairline);
  float cursorX = x + 3.0f;
  for(std::size_t i = 0; i < kButtons; ++i) {
    const Rect button {cursorX, y + 3.0f, widths[i], height - 6.0f};
    if(ui::contains(button, pointerX_, pointerY_)) fill(renderer, button, theme().hoverBg);
    ui::TextStyle label = style;
    label.strong = std::string_view(entries[i].id) == "bold";
    label.italic = std::string_view(entries[i].id) == "italic";
    if(std::string_view(entries[i].id) == "code") label.family = ui::FontFamily::Mono;
    const float labelX = button.x + (button.w - static_cast<float>(text.width(entries[i].label, label))) / 2.0f;
    text.draw(entries[i].label, labelX, y + 7.0f, theme().text, label);
    toolbar_.push_back({button, entries[i].id, entries[i].label});
    cursorX += widths[i];
  }
}

}
