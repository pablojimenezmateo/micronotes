#include "app/PageViewStyle.h"

#include "CoreAliases.h"
#include "app/FrameTrace.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/LinkTarget.h"
#include "ui/Fonts.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/ClipGuard.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

// Painting a laid-out note.
//
// The other half of `PageView`, split off for the reason `doc/LayoutQueries.
// cpp` was split off `doc/Layout.cpp`: the class is one class and its state
// stays private to it, but a reader after the caret arithmetic should not have
// to walk past the toolbar's hover states to reach it, and the two halves are
// different kinds of code. Nothing above this line touches an `SDL_Renderer`;
// nothing below it changes the layout.
//
// It is also what the 1,000-line ceiling `architecture_no_shell_source_is_a_
// catch_all` enforces was for: `PageView.cpp` reached 1,003 lines and the
// ceiling failed the build.
namespace micronotes::app {
namespace {

using micronotes::ui::Rect;
using micronotes::ui::TextRenderer;
using micronotes::ui::drawChevron;
using micronotes::ui::fill;
using micronotes::ui::hLine;
using micronotes::ui::stroke;
using micronotes::ui::theme;

using pageview::colorFor;
using pageview::toRect;
using pageview::toTextStyle;

}

// One block of the note: its list chrome, its lines and runs, and the pictures
// it named. Returns whether it was drawn at all -- a block above or below the
// viewport is skipped -- and adds the runs it emitted to `runs`.
//
// Extracted from `draw`, which was 233 lines and the longest function in
// `src/app/`. It stays in this translation unit deliberately: this is called
// once per visible block per frame, and Release carries no LTO.
bool PageView::drawBlock(SDL_Renderer* renderer, TextRenderer& text, std::size_t index,
                         const BlockPaint& paint, std::size_t& runs) {
  const auto& blocks = document_.blocks();
  const doc::SourceBlock& block = blocks[index];
  const doc::BlockLayout& layout = document_.layout(index);
  const float top = paint.oy + document_.blockTop(index);
  if(top + layout.height < paint.viewTop || top > paint.viewBottom) return false;

  const float left = paint.ox + layout.indent;
  const float bodyLine = layout.lines.empty() ? 0.0f : layout.lines.front().height;

  // List chrome stands in for the marker text, which is never shown.
  if(bodyLine > 0.0f) {
    const float markerY = top + (layout.lines.empty() ? 0.0f : layout.lines.front().y);
    if(block.kind == doc::BlockKind::Bullet) {
      ui::TextStyle style;
      style.size = ui::type().body;
      text.draw("•", left + 6.0f, markerY, theme().textSecondary, style);
    } else if(block.kind == doc::BlockKind::Ordered) {
      ui::TextStyle style;
      style.size = ui::type().body;
      const auto label = std::to_string(block.ordinal > 0 ? block.ordinal : 1) + ".";
      text.draw(label, left + 2.0f, markerY, theme().textSecondary, style);
    } else if(block.kind == doc::BlockKind::Todo) {
      Rect box {left + 3.0f, markerY + 4.0f, 14.0f, 14.0f};
      // A generous hit area: the drawn box is deliberately small.
      const Rect hit {box.x - 4.0f, box.y - 4.0f, box.w + 8.0f, box.h + 8.0f};
      checkboxes_.push_back({hit, block.start});
      // Including on a read-only page: a task is a control there too, and it
      // ticks. It used not to, and the hover was suppressed for exactly that
      // reason -- a box that lights up and then does nothing is worse than
      // one that never invited the click.
      const bool hot = ui::contains(hit, pointerX_, pointerY_);
      if(block.checked) {
        fill(renderer, box, theme().accent);
        const SDL_Color tick = theme().onAccent;
        SDL_SetRenderDrawColor(renderer, tick.r, tick.g, tick.b, tick.a);
        SDL_RenderLine(renderer, box.x + 3.5f, box.y + 7.0f, box.x + 6.0f, box.y + 9.5f);
        SDL_RenderLine(renderer, box.x + 6.0f, box.y + 9.5f, box.x + 10.5f, box.y + 4.5f);
      } else {
        // The box lights up under the pointer, because a control that never
        // reacts is one people do not learn is clickable.
        ui::stroke(renderer, box, hot ? theme().accent : theme().textMuted);
      }
    }
  }

  if(layout.complex) {
    Rect complexRect {paint.ox, top, columnWidth_, layout.height};
    if(hooks_.drawComplex) hooks_.drawComplex(block, complexRect);
    // Drawn, by md4c rather than by the run loop below.
    return true;
  }

  // A code block is kept inside the column: a long line scrolls off its own
  // right edge rather than out over the gutter. `std::optional` because the
  // guard is the scope, and this one has to end with the block rather than
  // with the loop body it lives in.
  std::optional<ui::ClipGuard> columnClip;
  if(block.kind == doc::BlockKind::Code) {
    columnClip.emplace(renderer, Rect {paint.ox, std::max(page_.y + 1.0f, top), columnWidth_,
                                       std::min(layout.height, page_.y + page_.h - top)});
  }
  for(std::size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
    const doc::VisualLine& line = layout.lines[lineIndex];
    const float lineY = top + line.y;
    if(lineY + line.height < paint.viewTop || lineY > paint.viewBottom) continue;
    // A line the *column* broke wears a mark at the point it broke, so a
    // reader can tell it from a line the file itself ended. Drawn on the line
    // that wrapped rather than on the one that continues it, at the trailing
    // edge, which is where the eye is when it runs out of room.
    if(lineIndex + 1 < layout.lines.size() && layout.lines[lineIndex + 1].continuation) {
      const float size = std::max(6.0f, line.height * 0.45f);
      const float markX = paint.ox + columnWidth_ - size - 2.0f;
      // Only where it has somewhere to go. A code line keeps
      // `doc::kWrapMarkReserve` clear for it; a paragraph does not, because it
      // breaks at a space and almost always leaves the room itself -- and on
      // the rare line that ends flush with the column, a mark drawn over the
      // last word would say less than the flush edge already does.
      float lineRight = paint.ox + layout.textLeft;
      for(const auto& run : layout.runsOf(line)) {
        lineRight = std::max(lineRight, paint.ox + run.rect.x + run.rect.w);
      }
      if(lineRight <= markX - 2.0f) {
        ui::drawWrapGlyph(renderer,
                          {markX, std::round(lineY + (line.height - size) / 2.0f), size, size},
                          theme().textDisabled);
      }
    }
    for(const auto& run : layout.runsOf(line)) {
      if(run.text.empty()) continue;
      ++runs;
      const ui::TextStyle style = toTextStyle(run.style);
      const float x = paint.ox + run.rect.x;
      if(run.role == doc::TextRole::Code && !run.isMarker) {
        fill(renderer, {x - 2.0f, lineY + 1.0f, run.rect.w + 4.0f, line.height - 2.0f}, theme().codeBackground);
      }
      SDL_Color ink = colorFor(run.role, block.kind);
      // A ticked task is done being read. The layout already struck it
      // through; muting the ink is the other half of saying so.
      if(block.kind == doc::BlockKind::Todo && block.checked &&
         run.role == doc::TextRole::Body) {
        ink = theme().textMuted;
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
          hLine(renderer, x, x + run.rect.w, lineY + line.height - 4.0f, theme().accent);
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
    const Rect box {paint.ox + image.rect.x, top + image.rect.y, image.rect.w, image.rect.h};
    if(box.y + box.h < paint.viewTop || box.y > paint.viewBottom) continue;
    if(hooks_.drawImage) hooks_.drawImage(image.target, box);
    links_.push_back({box, image.target, false});
  }
  return true;
}

void PageView::draw(SDL_Renderer* renderer, TextRenderer& text, const PageSelection& selection,
                    bool focused, std::span<const util::TextMatch> findMatches,
                    std::size_t activeMatch) {
  const perf::ScopeTimer timer("page.draw");
  perf::addCounter(perf::CounterId::PageDrawCalls);
  links_.clear();
  checkboxes_.clear();
  codeButtons_.clear();
  fill(renderer, rect_, theme().editorBackground);
  // Flat, with no outline. The page used to be drawn as a bordered card that
  // turned accent-coloured when focused, which made the entire writing surface
  // read as a selected text field -- and the border was doing all the work,
  // because pageSurface and the pane behind it are the same colour.
  ui::fill(renderer, page_, theme().editorBackground);

  const float ox = originX();
  const float oy = originY();
  const float viewTop = page_.y;
  const float viewBottom = page_.y + page_.h;
  // Everything below is clipped to the page; the scrollbar draws outside it.
  {
  const ui::ClipGuard pageClip(renderer, {page_.x + 1.0f, page_.y + 1.0f, page_.w - 2.0f, page_.h - 2.0f});

  if(selection.start != selection.end) {
    // Banded to the viewport, in document space. A selection reaching the whole
    // note is otherwise a pass over every visual row in it, per frame, to paint
    // the forty the window can show.
    const float bandTop = page_.y - oy;
    document_.selectionRectsInto(selection.start, selection.end, bandTop, bandTop + page_.h,
                                 &selectionRects_);
    for(const auto& rect : selectionRects_) {
      fill(renderer, toRect(rect, ox, oy), theme().selectionFill);
    }
  }
  // Only the blocks that reach the viewport. This used to walk the whole note
  // and test each block against the page, which made a draw cost the document
  // rather than the window: 10,801 blocks visited to draw 18 of them, every
  // frame, on a 235 KB note. The counters below are what said so, and they stay
  // to keep saying so.
  const auto [firstBlock, lastBlock] = visibleBlocks();

  if(!findMatches.empty()) {
    const perf::ScopeTimer findTimer("page.draw.find_highlight");
    drawFindHighlights(renderer, findMatches, activeMatch, firstBlock, lastBlock, ox, oy);
  }

  drawBlockDecorations(renderer, text);

  perf::addCounter(perf::CounterId::PageBlocksVisited, lastBlock - firstBlock);
  std::size_t blocksDrawn = 0;
  std::size_t runsDrawn = 0;
  BlockPaint paint;
  paint.ox = ox;
  paint.oy = oy;
  paint.viewTop = viewTop;
  paint.viewBottom = viewBottom;
  for(std::size_t i = firstBlock; i < lastBlock; ++i) {
    if(drawBlock(renderer, text, i, paint, runsDrawn)) ++blocksDrawn;
  }
  perf::addCounter(perf::CounterId::PageBlocksDrawn, blocksDrawn);
  perf::addCounter(perf::CounterId::PageRunsDrawn, runsDrawn);
  if(ScopedFrame* frame = ScopedFrame::current()) {
    frame->addBlocks(lastBlock - firstBlock, blocksDrawn, document_.lastRelaidBlocks());
    frame->addRuns(runsDrawn);
  }

  drawCodeChrome(renderer, text);
  }
  ui::drawVerticalScrollbar(renderer, page_, scroll_.scroll(), scroll_.maxScroll());
}

// Highlighting a find query used to be two O(document) costs on every frame the
// find bar was open: `std::string::find` over the whole note, and a
// `selectionRects` call -- which measures text -- for every match in the file,
// however far off screen it was. On a 235 KB note with a common word in it that
// is the whole frame.
//
// Both are gone. The search itself is not here at all any more: the shell owns
// one match list per (buffer, needle, options) and hands it to whichever
// surfaces are drawing, which is also what stopped the page, the raw pane and
// the match count being three separate answers. What is left is the second
// half -- only the matches inside the band of blocks the window is showing get
// a rect built for them, which is a pair of binary searches over a sorted list.
void PageView::drawFindHighlights(SDL_Renderer* renderer, std::span<const util::TextMatch> matches,
                                  std::size_t activeMatch, std::size_t firstBlock,
                                  std::size_t lastBlock, float ox, float oy) {
  const auto& blocks = document_.blocks();
  if(firstBlock >= lastBlock || blocks.empty()) return;
  // A match starting just before the first visible block can still reach into
  // it, so the band opens one block early. It cannot reach further than that:
  // a block boundary is a line boundary, and a needle carrying a newline is one
  // the find bar's single-line field cannot hold.
  const std::size_t from = blocks[firstBlock > 0 ? firstBlock - 1 : 0].start;
  const std::size_t to = blocks[std::min(lastBlock, blocks.size()) - 1].end();
  const auto byStart = [](const util::TextMatch& match, std::size_t offset) {
    return match.start < offset;
  };
  const auto begin = std::lower_bound(matches.begin(), matches.end(), from, byStart);
  const auto end = std::lower_bound(matches.begin(), matches.end(), to, byStart);
  perf::addCounter(perf::CounterId::PageFindHighlightsDrawn,
                   static_cast<std::uint64_t>(end - begin));
  const float bandTop = page_.y - oy;
  for(auto it = begin; it != end; ++it) {
    // The one the reader is on takes the stronger of the two match colours and
    // the rest the resting one, with the same outline over both. The outline
    // alone was the distinction first, and at a glance over a paragraph of hits
    // it was no distinction at all -- a one-pixel edge in a colour a pixel away
    // from the fill beside it.
    const bool active = static_cast<std::size_t>(it - matches.begin()) == activeMatch;
    document_.selectionRectsInto(it->start, it->end, bandTop, bandTop + page_.h, &selectionRects_);
    for(const auto& rect : selectionRects_) {
      const Rect hit = toRect(rect, ox, oy);
      fill(renderer, hit, active ? theme().searchMatchActive : theme().searchMatch);
      stroke(renderer, hit, theme().searchMatchActive);
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
    const float top = oy + document_.blockTop(i);
    const float left = ox + layout.indent;

    if(block.kind == doc::BlockKind::Code) {
      if(layout.complex || top + layout.height < viewTop || top > viewBottom) continue;
      const Rect codeRect {left, top + 2.0f, columnWidth_ - layout.indent, std::max(8.0f, layout.height - 8.0f)};
      fill(renderer, codeRect, theme().codeBackground);
      continue;
    }

    if(block.kind == doc::BlockKind::Divider) {
      if(top + layout.height < viewTop || top > viewBottom) continue;
      // One rule, centred in the air the layout reserves for it.
      const float middle = std::round(top + layout.height / 2.0f);
      hLine(renderer, left, ox + columnWidth_, middle, theme().border);
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
      fill(renderer, {left, top + 2.0f, 3.0f, height}, theme().border);
      continue;
    }

    const ui::CalloutStyle style = ui::calloutStyle(block.info(document_.source()));
    const Rect callout {left, top, columnWidth_ - layout.indent, height + 2.0f};
    // No rule down the left edge. The tint is already the whole shape, and a
    // bar beside it makes the box read as a quote wearing a colour rather than
    // as a callout.
    ui::drawSurface(renderer, callout, style.surface, style.surface);
    if(!layout.calloutTitle) continue;

    // The head line is the title: a mark in the gutter, and the kind's own name
    // when the author did not write one after `[!KIND]`.
    const float lineY = callout.y + (layout.lines.empty() ? 8.0f : layout.lines.front().y);
    const float lineH = layout.lines.empty() ? 20.0f : layout.lines.front().height;
    // Sized and placed to sit inside the quote gutter the layout already
    // reserves, so the mark never crowds the title beside it. The size and the
    // inset are `ui::Metrics`' because the reading pane draws the same mark.
    fill(renderer,
                {std::round(callout.x + ui::kCalloutMarkInset),
                 std::round(lineY + (lineH - ui::kCalloutMarkSize) / 2.0f),
                 ui::kCalloutMarkSize, ui::kCalloutMarkSize},
                style.accent);

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
    if(layout.complex) continue;
    const float top = oy + document_.blockTop(i);
    if(top + layout.height < page_.y || top > page_.y + page_.h) continue;

    const float right = ox + columnWidth_ - 6.0f;
    // In the band the layout reserves above a labelled block, not on its first
    // line of code. See `styleForBlock`.
    const float y = top + 3.0f;
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
      ui::drawSurface(renderer, button, hot ? theme().overlayBackground : theme().surfaceRaised,
                             hot ? theme().border : theme().surfaceRaised);
      text.draw(copy, button.x + 7.0f, button.y + 3.0f, hot ? theme().textPrimary : theme().textSecondary, label);
    }

    if(!block.hasInfo()) continue;
    const std::string_view info = block.info(document_.source());
    text.draw(info, button.x - static_cast<float>(text.width(info, label)) - 10.0f, y + 3.0f,
              theme().textMuted, label);
  }
}
}
