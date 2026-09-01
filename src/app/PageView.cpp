#include "app/PageView.h"

#include "doc/Fold.h"
#include "ui/Fonts.h"
#include "ui/Settings.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace micronotes::app {
namespace {

using micronotes::ui::Rect;
using micronotes::ui::TextRenderer;
using micronotes::ui::drawDisclosure;
using micronotes::ui::fill;
using micronotes::ui::hLine;
using micronotes::ui::stroke;
using micronotes::ui::theme;

constexpr float kPagePadding = 8.0f;
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

// Hand-drawn: the vendored UI face carries no disclosure glyph, and a missing
// glyph in the gutter would read as a rendering bug.
SDL_Color colorFor(doc::TextRole role) {
  switch(role) {
    case doc::TextRole::Marker: return theme().dim;
    case doc::TextRole::Link:
    case doc::TextRole::WikiLink: return theme().accent;
    case doc::TextRole::WikiLinkUnresolved: return theme().linkPending;
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

doc::TypeMetrics typeMetrics() {
  doc::TypeMetrics metrics;
  metrics.body = ui::type().body;
  metrics.mono = ui::type().mono;
  for(int level = 1; level <= 6; ++level) metrics.heading[level - 1] = ui::headingSize(level);
  metrics.lineHeightRatio = ui::type().lineHeightRatio;
  return metrics;
}

Rect toRect(const doc::Rect& rect, float originX, float originY) {
  return {rect.x + originX, rect.y + originY, rect.w, rect.h};
}

Rect scrollTrack(Rect viewport) {
  return {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, std::max(24.0f, viewport.h - 18.0f)};
}

void drawScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return;
  const Rect track = scrollTrack(viewport);
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  fill(renderer, track, theme().scrollTrack);
  Rect thumb {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
  fill(renderer, thumb, theme().scrollThumb);
  stroke(renderer, thumb, theme().scrollThumbBorder);
}

}

void PageView::setHooks(PageViewHooks hooks) {
  hooks_ = std::move(hooks);
}

const doc::DocumentLayout& PageView::document() const {
  return document_;
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
  return std::max(0, static_cast<int>(std::ceil(document_.totalHeight() - visible)));
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
  rect_ = rect;
  page_ = {rect.x + kPagePadding, rect.y + kPagePadding, rect.w - kPagePadding * 2.0f, rect.h - kPagePadding * 3.5f};
  const float available = std::max(120.0f, page_.w - 28.0f);
  columnWidth_ = std::min(available, ui::pageWidthPx());
  float left = page_.x + std::round((page_.w - columnWidth_) / 2.0f);
  if(left < page_.x + kGutterWidth) {
    columnWidth_ = std::max(120.0f, page_.w - kGutterWidth - 14.0f);
    left = page_.x + kGutterWidth;
  }
  columnLeft_ = left;
  contentTop_ = page_.y + kContentTopPadding;

  // Installing metrics drops every cached block layout, so it happens only when
  // the faces actually change, not once a frame.
  text_ = &text;
  if(std::abs(text.displayScale() - metricsScale_) > 0.001f) {
    metricsScale_ = text.displayScale();
    doc::Metrics metrics;
    metrics.measure = [this](std::string_view value, const doc::RunStyle& style) {
      return static_cast<float>(text_->width(value, toTextStyle(style)));
    };
    metrics.lineHeight = [this](const doc::RunStyle& style) {
      const float ratio = style.size >= ui::type().h3 ? 1.25f : ui::type().lineHeightRatio;
      const float fromRatio = std::round((style.size > 0.0f ? style.size : ui::type().body) * ratio);
      return std::max(static_cast<float>(text_->lineHeight(toTextStyle(style))), fromRatio);
    };
    metrics.measureComplex = [this](const doc::SourceBlock& block, float width) {
      return hooks_.measureComplex ? hooks_.measureComplex(block, width) : 0.0f;
    };
    document_.setMetrics(std::move(metrics));
  }

  doc::LayoutOptions options;
  options.width = columnWidth_;
  options.fontScale = text.displayScale();
  options.type = typeMetrics();
  options.caretOffset = caret;
  options.rawOffset = rawOffset_ ? *rawOffset_ : doc::DocumentLayout::kNone;
  options.folded = folds_.collapsed;
  options.wikiLinkResolves = hooks_.wikiLinkResolves;
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
    document_.update(source, options);
  }
  scroll_ = std::clamp(scroll_, 0, maxScroll());
}

void PageView::setFolds(PageFolds folds) {
  folds_ = std::move(folds);
}

void PageView::revealCaret(std::size_t offset) {
  const auto caret = document_.caretRect(offset);
  const float visible = std::max(1.0f, page_.h - kContentTopPadding * 2.0f);
  if(caret.y < static_cast<float>(scroll_)) {
    scroll_ = static_cast<int>(std::floor(caret.y));
  } else if(caret.y + caret.h > static_cast<float>(scroll_) + visible) {
    scroll_ = static_cast<int>(std::ceil(caret.y + caret.h - visible));
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
  links_.clear();
  checkboxes_.clear();
  toolbar_.clear();
  gutter_.clear();
  foldHits_.clear();
  codeButtons_.clear();
  fill(renderer, rect_, theme().editorBg);
  ui::drawSurface(renderer, page_, theme().pageSurface, focused ? theme().accentDim : theme().hairline);

  const float ox = originX();
  const float oy = originY();
  const float viewTop = page_.y;
  const float viewBottom = page_.y + page_.h;
  const SDL_Rect pageClip = ui::clipRect({page_.x + 1.0f, page_.y + 1.0f, page_.w - 2.0f, page_.h - 2.0f});
  SDL_SetRenderClipRect(renderer, &pageClip);

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
    for(const auto& rect : document_.selectionRects(selection.start, selection.end)) {
      fill(renderer, toRect(rect, ox, oy), theme().selectionBg);
    }
  }
  if(!findQuery.empty()) {
    const std::string& source = document_.source();
    std::size_t at = source.find(findQuery);
    while(at != std::string::npos) {
      for(const auto& rect : document_.selectionRects(at, at + findQuery.size())) {
        const Rect hit = toRect(rect, ox, oy);
        fill(renderer, hit, theme().findBg);
        stroke(renderer, hit, theme().findBorder);
      }
      at = source.find(findQuery, at + std::max<std::size_t>(1, findQuery.size()));
    }
  }

  drawBlockDecorations(renderer, text);

  const auto& blocks = document_.blocks();
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    const doc::SourceBlock& block = blocks[i];
    const doc::BlockLayout& layout = document_.layout(i);
    const float top = oy + document_.blockTop(i);
    if(top + layout.height < viewTop || top > viewBottom) continue;

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
        Rect box {left + 3.0f, markerY + 4.0f, 13.0f, 13.0f};
        // A generous hit area: the drawn box is deliberately small.
        checkboxes_.push_back({{box.x - 4.0f, box.y - 4.0f, box.w + 8.0f, box.h + 8.0f}, block.start});
        if(block.checked) {
          fill(renderer, box, theme().accent);
          const SDL_Color tick = theme().onAccent;
          SDL_SetRenderDrawColor(renderer, tick.r, tick.g, tick.b, tick.a);
          SDL_RenderLine(renderer, box.x + 3.0f, box.y + 6.5f, box.x + 5.5f, box.y + 9.0f);
          SDL_RenderLine(renderer, box.x + 5.5f, box.y + 9.0f, box.x + 10.0f, box.y + 4.0f);
        } else {
          stroke(renderer, box, theme().muted);
        }
      }
    }

    if(layout.complex) {
      Rect complexRect {ox, top, columnWidth_, layout.height};
      if(hooks_.drawComplex) hooks_.drawComplex(block, complexRect);
      continue;
    }

    const bool clipToColumn = block.kind == doc::BlockKind::Code || layout.raw;
    if(clipToColumn) {
      const SDL_Rect columnClip = ui::clipRect({ox, std::max(page_.y + 1.0f, top), columnWidth_,
                                                std::min(layout.height, page_.y + page_.h - top)});
      SDL_SetRenderClipRect(renderer, &columnClip);
    }
    for(const auto& line : layout.lines) {
      const float lineY = top + line.y;
      if(lineY + line.height < viewTop || lineY > viewBottom) continue;
      for(const auto& run : line.runs) {
        if(run.text.empty()) continue;
        const ui::TextStyle style = toTextStyle(run.style);
        const float x = ox + run.rect.x;
        if(run.role == doc::TextRole::Code && !run.isMarker) {
          fill(renderer, {x - 2.0f, lineY + 1.0f, run.rect.w + 4.0f, line.height - 2.0f}, theme().codeBg);
        }
        const SDL_Color ink = colorFor(run.role, block.kind);
        text.draw(run.text, x, lineY, ink, style);
        if(run.style.strike) {
          hLine(renderer, x, x + run.rect.w, lineY + line.height * 0.45f, ink);
        }
        if(run.linkIndex >= 0 && run.linkIndex < static_cast<int>(layout.links.size())) {
          hLine(renderer, x, x + run.rect.w, lineY + line.height - 4.0f, theme().accentDim);
          const bool wiki = run.role == doc::TextRole::WikiLink ||
                            run.role == doc::TextRole::WikiLinkUnresolved;
          links_.push_back({{x, lineY, run.rect.w, line.height},
                            layout.links[static_cast<std::size_t>(run.linkIndex)], wiki});
        }
      }
    }
    if(clipToColumn) SDL_SetRenderClipRect(renderer, &pageClip);
  }

  if(focused && !blockSelection_.active) {
    const auto rect = document_.caretRect(caret);
    const Rect caretRect = toRect(rect, ox, oy);
    if(caretRect.y + caretRect.h >= viewTop && caretRect.y <= viewBottom) {
      fill(renderer, {caretRect.x, caretRect.y + 1.0f, 2.0f, std::max(4.0f, caretRect.h - 2.0f)}, theme().accent);
    }
  }
  drawCodeChrome(renderer, text);
  drawDropIndicator(renderer);
  SDL_SetRenderClipRect(renderer, nullptr);
  drawFoldControls(renderer);
  drawGutter(renderer, text);
  drawToolbar(renderer, text, selection);
  drawScrollbar(renderer, page_, scroll_, maxScroll());
}

void PageView::drawBlockDecorations(SDL_Renderer* renderer, TextRenderer& text) {
  const float ox = originX();
  const float oy = originY();
  const float viewTop = page_.y;
  const float viewBottom = page_.y + page_.h;
  const auto& blocks = document_.blocks();

  for(std::size_t i = 0; i < blocks.size(); ++i) {
    const doc::SourceBlock& block = blocks[i];
    const doc::BlockLayout& layout = document_.layout(i);
    if(layout.hidden) continue;
    const float top = oy + document_.blockTop(i);
    const float left = ox + layout.indent;

    if(block.kind == doc::BlockKind::Code) {
      if(layout.complex || top + layout.height < viewTop || top > viewBottom) continue;
      const Rect codeRect {left, top + 2.0f, columnWidth_ - layout.indent, std::max(8.0f, layout.height - 8.0f)};
      ui::drawSurface(renderer, codeRect, theme().codeBg, theme().hairline);
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

    const ui::CalloutStyle style = ui::calloutStyle(block.info);
    const Rect callout {left, top, columnWidth_ - layout.indent, height + 2.0f};
    ui::drawSurface(renderer, callout, style.surface, style.surface);
    fill(renderer, {callout.x, callout.y, 3.0f, callout.h}, style.accent);
    if(document_.layout(i).revealed || block.info.empty()) continue;
    // The kind reads as a badge in the corner rather than a word in the text:
    // the first line of a callout is the user's sentence, not our label.
    std::string name = block.info;
    for(std::size_t c = 1; c < name.size(); ++c) {
      name[c] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[c])));
    }
    ui::TextStyle label;
    label.size = ui::type().tiny;
    label.strong = true;
    const float badgeLeft = callout.x + callout.w - static_cast<float>(text.width(name, label)) - 9.0f;
    // Skipped rather than overlapped when the first line reaches that far: the
    // accent already says which kind this is, and the words matter more.
    float firstLineRight = ox + layout.textLeft;
    if(!layout.lines.empty()) {
      for(const auto& run : layout.lines.front().runs) {
        if(!run.text.empty()) firstLineRight = std::max(firstLineRight, ox + run.rect.x + run.rect.w);
      }
    }
    if(firstLineRight + 8.0f > badgeLeft) continue;
    text.draw(name, badgeLeft, callout.y + 5.0f, style.accent, label);
  }
}

void PageView::drawCodeChrome(SDL_Renderer* renderer, TextRenderer& text) {
  const float ox = originX();
  const float oy = originY();
  const auto& blocks = document_.blocks();
  ui::TextStyle label;
  label.size = ui::type().tiny;

  for(std::size_t i = 0; i < blocks.size(); ++i) {
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
    codeButtons_.push_back({button, block.start});
    const bool hot = ui::contains(button, pointerX_, pointerY_);
    // Drawn over the code, so it needs its own ground to stay readable.
    ui::drawSurface(renderer, button, hot ? theme().surfaceElevated : theme().codeBg, hot ? theme().hairline : theme().codeBg);
    text.draw(copy, button.x + 7.0f, button.y + 3.0f, hot ? theme().text : theme().dim, label);

    if(block.info.empty()) continue;
    text.draw(block.info, button.x - static_cast<float>(text.width(block.info, label)) - 10.0f, y + 3.0f,
              theme().dim, label);
  }
}

void PageView::drawFoldControls(SDL_Renderer* renderer) {
  const float oy = originY();
  const auto& blocks = document_.blocks();
  const auto hovered = document_.blockAt(pointerY_ - oy);
  const bool onPage = pointerX_ >= page_.x && pointerX_ <= page_.x + page_.w &&
                      pointerY_ >= page_.y && pointerY_ <= page_.y + page_.h;

  for(std::size_t i = 0; i < blocks.size(); ++i) {
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
  const auto rects = document_.selectionRects(selection.start, selection.end);
  if(rects.empty()) return;

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

  const Rect first = toRect(rects.front(), originX(), originY());
  float x = std::clamp(first.x - 8.0f, page_.x + 6.0f, page_.x + page_.w - width - 6.0f);
  float y = first.y - height - 8.0f;
  if(y < page_.y + 4.0f) {
    // No room above: sit below the selection rather than off the page.
    const Rect last = toRect(rects.back(), originX(), originY());
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
