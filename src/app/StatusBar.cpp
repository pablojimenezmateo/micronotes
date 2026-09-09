#include "app/StatusBar.h"

#include "app/Chrome.h"
#include "app/Commands.h"
#include "app/Shell.h"

#include "core/perf/Perf.h"
#include "core/util/Utf8.h"

#include "ui/Actions.h"
#include "ui/Fonts.h"
#include "ui/Metrics.h"
#include "ui/Painter.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::TextRenderer;
using ui::fill;
using ui::theme;

// The unsaved mark, and the air either side of it.
constexpr float kDotSize = 6.0f;
// The gap between two segments, and the rule drawn down the middle of it.
constexpr float kSegmentGap = ui::kSpace4;

std::string plural(std::size_t count, std::string_view noun) {
  return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
}

// Where each segment was laid out, so the paint, the hover and the press are
// one geometry. The bar is packed from both ends, which means a segment's x
// depends on the widths of every segment outside it -- exactly the arithmetic
// that goes wrong when a hit test writes it out a second time.
struct StatusPlacement {
  std::array<Rect, kStatusSegmentCount> boxes {};
  // What is left in the middle once both groups have taken their room. Where a
  // message goes, and nothing else.
  Rect notice;
};

// The band a segment lifts and answers over: wider than its text, so a
// clickable segment reads as a target rather than as a word that happens to
// have changed colour -- and so the press does not demand the reader hit the
// glyphs exactly.
Rect hoverBand(Rect box) {
  return {box.x - ui::kSpace2, box.y + 1.0f, box.w + ui::kSpace2 * 2.0f, box.h - 2.0f};
}

StatusPlacement placeStatusSegments(const StatusSegments& segments, const TextRenderer& text,
                                    Rect rect) {
  const ui::TextStyle style = ui::chromeStyle();
  StatusPlacement placement;
  // The dot sits before the first segment rather than inside it, so the save
  // state's own text starts on the same column whatever it says.
  float left = rect.x + ui::kSpace3 + kDotSize + ui::kSpace2;
  for(std::size_t i = 0; i < kFirstTrailingSegment; ++i) {
    if(!segments[i].visible) continue;
    const float width = static_cast<float>(text.width(segments[i].text, style));
    placement.boxes[i] = {left, rect.y, width, rect.h};
    left += width + kSegmentGap;
  }

  float right = rect.x + rect.w - ui::kSpace3;
  for(std::size_t i = kStatusSegmentCount; i-- > kFirstTrailingSegment;) {
    if(!segments[i].visible) continue;
    const float width = static_cast<float>(text.width(segments[i].text, style));
    placement.boxes[i] = {right - width, rect.y, width, rect.h};
    right -= width + kSegmentGap;
  }

  placement.notice = {left, rect.y, std::max(0.0f, right - left - kSegmentGap), rect.h};
  return placement;
}

}

CaretPlace caretPlaceIn(std::string_view text, std::size_t cursor) {
  const std::size_t at = std::min(cursor, text.size());
  perf::addCounter(perf::CounterId::StatusTextScans);
  perf::addCounter(perf::CounterId::StatusTextScanBytes, at);
  CaretPlace place;
  // One pass for the line, from the top -- a backwards scan for the last
  // newline would give the column in one step and the line in none, and the
  // line is the half a reader is actually after.
  //
  // Through `memchr` rather than a byte loop with a branch in it. That is not
  // decoration: this runs once per caret move, and on a 200 KB note the loop
  // form measured 63 us against the 21 us the live page's whole layout costs --
  // the largest single thing a keystroke did, which is exactly the shape the
  // word count was removed from the bar for being. See `docs/performance.md`,
  // "A segmented status bar, and the readout that paid for it".
  std::size_t lineStart = 0;
  const char* const base = text.data();
  while(lineStart < at) {
    const void* found = std::memchr(base + lineStart, '\n', at - lineStart);
    if(found == nullptr) break;
    ++place.line;
    lineStart = static_cast<std::size_t>(static_cast<const char*>(found) - base) + 1;
  }
  // Code points, not bytes: a column of 14 on a line of seven accented letters
  // is a number about the file's encoding rather than about where the caret is.
  for(std::size_t i = lineStart; i < at; i = util::nextBoundary(text, i)) ++place.column;
  return place;
}

std::size_t codePointsIn(std::string_view text, std::size_t start, std::size_t end) {
  const std::size_t from = std::min(start, text.size());
  const std::size_t to = std::min(end, text.size());
  if(to <= from) return 0;
  perf::addCounter(perf::CounterId::StatusTextScans);
  perf::addCounter(perf::CounterId::StatusTextScanBytes, to - from);
  std::size_t count = 0;
  for(std::size_t i = from; i < to; i = util::nextBoundary(text, i)) ++count;
  return count;
}

StatusSegments statusSegments(const UiRuntime& ui) {
  StatusSegments segments;
  const bool noteOpen = !ui.state.selection().noteId.empty();

  StatusSegmentValue& save = segments[static_cast<std::size_t>(StatusSegment::Save)];
  save.visible = noteOpen;
  save.text = ui.editor.dirty() ? "Unsaved" : "Saved";
  save.tone = ui.editor.dirty() ? StatusTone::Unsaved : StatusTone::Normal;
  save.tooltip = ui.editor.dirty() ? "Unsaved changes -- click to save now  " + ui::keysFor(ui::ActionId::Save)
                                   : "Everything here is on disk";
  // Clickable even when clean, because "is it saved" is the question this
  // segment answers and pressing it is how a reader makes the answer yes. A
  // save with nothing to write costs nothing.
  save.command = "save";

  StatusSegmentValue& position = segments[static_cast<std::size_t>(StatusSegment::Position)];
  // Only where the *source* is on screen, which is the raw pane and the split.
  //
  // A line and a column are coordinates in the note's Markdown. In the reading
  // pane there is no caret to have them; in the live surface there is one, but
  // what the reader is looking at is the rendered text and "Ln 42" names a line
  // of a file they cannot see -- a number that moves for reasons that are not
  // on screen. It also costs something: the line is a pass over the note up to
  // the caret, 28 us on a 200 KB one, and paying it in the default pane to
  // report a coordinate nothing there is measured in is the trade this makes
  // the other way. See `docs/performance.md`.
  position.visible = noteOpen && (ui.paneMode() == ui::PaneMode::Editor ||
                                  ui.paneMode() == ui::PaneMode::Split);
  if(position.visible) {
    const CaretPlaceKey key {ui.editor.revision(), ui.editor.cursor()};
    const CaretPlace* place = ui.statusBar.caretPlace.get(key);
    if(place == nullptr) {
      place = &ui.statusBar.caretPlace.store(key, caretPlaceIn(ui.editor.text(), ui.editor.cursor()));
    }
    position.text = "Ln " + std::to_string(place->line) + ", Col " + std::to_string(place->column);
    position.tooltip = "Where the caret is in the note's Markdown";
  }

  StatusSegmentValue& selection = segments[static_cast<std::size_t>(StatusSegment::Selection)];
  selection.visible = noteOpen && ui.editor.hasSelection();
  if(selection.visible) {
    const SelectionSpanKey key {ui.editor.revision(), ui.editor.selectionStart(),
                                ui.editor.selectionEnd()};
    const std::size_t* size = ui.statusBar.selectionSize.get(key);
    if(size == nullptr) {
      size = &ui.statusBar.selectionSize.store(
        key, codePointsIn(ui.editor.text(), key.start, key.end));
    }
    selection.text = plural(*size, "character") + " selected";
    selection.tooltip = "What Copy, Cut and the formatting keys would act on";
  }

  StatusSegmentValue& words = segments[static_cast<std::size_t>(StatusSegment::Words)];
  words.visible = noteOpen;
  if(words.visible) {
    // A word is a run of non-space bytes, which is what every editor's status
    // bar means by it and what a reader checking a word budget expects. The
    // editor carries the count across each edit, so there is nothing to walk.
    words.text = plural(ui.editor.wordCount(), "word");
    // Characters are bytes of the note as stored, and the tooltip says so
    // rather than the bar claiming a count it has not made.
    words.tooltip = plural(ui.editor.text().size(), "byte") + " on disk";
  }

  StatusSegmentValue& pane = segments[static_cast<std::size_t>(StatusSegment::PaneMode)];
  pane.visible = noteOpen;
  pane.text = paneModeName(ui.paneMode());
  pane.tooltip = "Click to cycle the four views  " + ui::keysFor(ui::ActionId::CyclePane);
  pane.command = "cycle-pane";
  return segments;
}

void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  const StatusSegments segments = statusSegments(ui);
  const ui::TextStyle style = ui::chromeStyle();
  const float baseline = ui::textTop(rect, text, style);
  const StatusPlacement placement = placeStatusSegments(segments, text, rect);

  fill(renderer, {rect.x + ui::kSpace3, std::round(rect.y + (rect.h - kDotSize) / 2.0f), kDotSize, kDotSize},
       ui.editor.dirty() ? theme().warn : theme().accent);

  for(std::size_t i = 0; i < kStatusSegmentCount; ++i) {
    const StatusSegmentValue& segment = segments[i];
    if(!segment.visible || ui::empty(placement.boxes[i])) continue;
    const Rect box = placement.boxes[i];
    const Rect band = hoverBand(box);
    const bool hovered = segment.clickable() && ui.pointer.over(band);
    if(hovered) fill(renderer, band, theme().rowHighlight);
    if(!segment.tooltip.empty()) ui.pointer.offerTooltip(band, segment.tooltip);
    SDL_Color ink = theme().chromeTextSecondary;
    if(segment.tone == StatusTone::Unsaved) ink = theme().warn;
    else if(hovered) ink = theme().chromeText;
    text.draw(segment.text, box.x, baseline, ink, style);
  }

  // The message, in what is left in the middle, and only while it is fresh.
  // Ellipsized rather than allowed to run into the segments either side: those
  // are the things that are still true, and a message is the thing that was.
  if(!ui.status.showing(SDL_GetTicks()) || placement.notice.w <= 0.0f) return;
  text.draw(ui::ellipsizeToWidth(text, ui.status.text, static_cast<int>(placement.notice.w), style),
            placement.notice.x, baseline, theme().chromeText, style);
}

namespace {

// The command a press at this point would run, empty when there is none. The
// one walk the cursor shape and the press both go through.
std::string statusCommandAt(TextRenderer& text, const UiRuntime& ui, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return {};
  const StatusSegments segments = statusSegments(ui);
  const StatusPlacement placement = placeStatusSegments(segments, text, rect);
  for(std::size_t i = 0; i < kStatusSegmentCount; ++i) {
    if(!segments[i].visible || !segments[i].clickable()) continue;
    const Rect box = placement.boxes[i];
    if(ui::contains(hoverBand(box), x, y)) return segments[i].command;
  }
  return {};
}

}

bool statusBarHasControlAt(TextRenderer& text, const UiRuntime& ui, Rect rect, float x, float y) {
  return !statusCommandAt(text, ui, rect, x, y).empty();
}

bool pressStatusBar(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y, Uint8 button) {
  if(!ui::contains(rect, x, y)) return false;
  if(button != SDL_BUTTON_LEFT) return true;
  const std::string command = statusCommandAt(text, ui, rect, x, y);
  if(!command.empty()) performCommand(ui, command);
  return true;
}

}
