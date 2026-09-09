#include "CoreAliases.h"
#include "core/editor/SingleLineView.h"
#include "ui/Overlay.h"

#include "core/perf/PerformanceCounters.h"

#include "core/util/Fuzzy.h"
#include "ui/Metrics.h"
#include "ui/RowCursor.h"
#include "ui/TagColors.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/Widgets.h"
#include "ui/ClipGuard.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

// A row, and the field over it. The row is the menu popup's row: the palette,
// the context menus and the menu bar's popups are the same object -- a list of
// commands with their accelerators -- and they were three heights, so a context
// menu and the palette that can run the same command looked unrelated.
constexpr float kRowHeight = kMenuPopupItemHeight;
constexpr float kFieldHeight = 26.0f;
constexpr float kPadding = 10.0f;
// Inside a row. Written out as 10 and 20 at seven sites; the gap between a
// row's two trailing pieces went with them into `ui::drawMenuRow`, which is now
// the one place a popup row is painted.
constexpr float kRowPadX = kPadding;
// A Confirm's two buttons. Equal width, because they are two answers to one
// question and the wider of two buttons reads as the recommended one -- which
// on a deletion is the wrong recommendation to make by accident.
constexpr float kButtonWidth = 96.0f;
// A colour picker's grid. Six across so twelve swatches are two rows -- both in
// the eye at once, which is the whole reason it is a grid and not a list -- and
// a cell big enough to be a colour rather than a pixel.
constexpr int kSwatchColumns = 6;
constexpr float kSwatchCell = 30.0f;
constexpr float kSwatchGap = 4.0f;
// The header band a titled overlay wears -- see `drawTitledCard`. The menu
// bar's height, because it is the same kind of surface and two chrome strips
// that differ by three pixels read as a mistake.

// A row can be landed on when it is a real row that is not disabled. Section
// headings are listed as disabled rows and separators are not rows at all, so
// both are drawn and both are skipped -- which is the whole reason the
// distinction is a field rather than an empty label.
bool selectable(const OverlayItem& item) {
  return item.enabled && !item.separator;
}

// The menu bar's own popups measure their rows with `menuRowHeight` too, so a
// context menu and a menu-bar menu are one shape rather than two that happen to
// use the same numbers.
float rowHeight(const OverlayItem& item) {
  return menuRowHeight(item.separator);
}

// The three faces an overlay sets, named once so the layout reserves room in
// the same style the draw paints in. They had drifted: the hint's height was
// reserved in Sans at the `tiny` size and drawn in Mono at 0.85 of `chrome`,
// and the title's in Sans/`small`-bold and drawn in Mono/`chrome`-bold. Both
// pairs happen to be within a pixel, which is exactly why nobody noticed.
TextStyle titleFace() {
  return {FontFamily::Mono, true, false, type().chrome};
}
TextStyle hintFace() {
  return chromeSmallStyle();
}

}

bool OverlayStack::active() const {
  return !stack_.empty();
}

const Overlay* OverlayStack::top() const {
  return stack_.empty() ? nullptr : &stack_.back();
}

Overlay* OverlayStack::top() {
  return stack_.empty() ? nullptr : &stack_.back();
}

void OverlayStack::open(Overlay overlay) {
  overlay.rows.rebase();
  // An overlay that opens on a choice already in force starts *there* rather
  // than at the top: a colour picker whose keyboard cursor begins on the first
  // swatch has thrown away the one piece of context it had, and the reader has
  // to find their own colour again before they can step off it.
  const bool startOnCurrent = overlay.current >= 0 &&
    overlay.current < static_cast<int>(overlay.items.size());
  overlay.highlighted = startOnCurrent ? overlay.current : 0;
  stack_.push_back(std::move(overlay));
  if(!startOnCurrent) resetHighlight();
}

void OverlayStack::close() {
  if(!stack_.empty()) stack_.pop_back();
}

const std::vector<int>& OverlayStack::visibleIndices(const Overlay& overlay) const {
  const std::string& query = overlay.value.text();
  if(overlay.filterCacheValid && overlay.filterCacheQuery == query) {
    perf::addCounter(perf::CounterId::OverlayFilterReused);
    return overlay.filterCache;
  }
  perf::addCounter(perf::CounterId::OverlayFilterRuns);
  overlay.filterCacheValid = true;
  overlay.filterCacheQuery = query;
  // Reused rather than reallocated: the vector is refilled at every keystroke
  // of a palette the size of the library.
  overlay.filterCache.clear();
  if(!overlay.filterable || query.empty()) {
    overlay.filterCache.reserve(overlay.items.size());
    for(int i = 0; i < static_cast<int>(overlay.items.size()); ++i) overlay.filterCache.push_back(i);
    return overlay.filterCache;
  }
  std::vector<std::pair<int, int>> scored;  // (score, index)
  for(int i = 0; i < static_cast<int>(overlay.items.size()); ++i) {
    const auto& item = overlay.items[static_cast<std::size_t>(i)];
    perf::addCounter(perf::CounterId::OverlayFilterItemsScored);
    auto score = util::fuzzyScore(item.label, query);
    if(!score && !item.detail.empty()) score = util::fuzzyScore(item.detail, query);
    if(score) scored.emplace_back(*score, i);
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  overlay.filterCache.reserve(scored.size());
  for(const auto& [_, index] : scored) overlay.filterCache.push_back(index);
  return overlay.filterCache;
}

OverlayStack::Layout OverlayStack::layoutFor(Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const {
  Layout layout;
  // A reference into the standing answer: the layout only reads it, and nothing
  // in here changes the query it is keyed on. On a "Go to note" palette over a
  // large library this is the difference between one vector and a copy of it per
  // frame.
  const std::vector<int>& indices = visibleIndices(overlay);
  const bool field = overlay.takesTypedText();

  // A titled *band* rather than a line of text with a gap under it, so its
  // height is the band's and not the type's plus a fudge. An anchored context
  // menu keeps no header: it is a list of commands, not a question.
  const float titleH = overlay.title.empty()
                         ? 0.0f
                         : std::max(kTitleBandHeight,
                                    static_cast<float>(text.lineHeight(titleFace())) + kSpace2);
  const float fieldH = field ? kFieldHeight + kPadding : 0.0f;
  const float hintProbe = overlay.hint.empty() ? 0.0f
                                               : static_cast<float>(text.lineHeight(hintFace())) + kSpace2;
  // What is left of the window below where the panel starts, once its own
  // chrome is paid for. Measured from the top the panel will actually take, or
  // a tall list would be laid out past the bottom of the window.
  const float panelTop =
    overlay.anchored ? kCardWindowInset : std::max(60.0f, static_cast<float>(windowHeight) * 0.18f);
  const float room = static_cast<float>(windowHeight) - panelTop - kCardWindowInset
                   - titleH - (field ? kFieldHeight + kPadding : 0.0f) - hintProbe - kPadding * 2.0f;
  // How much of the list fits, walked rather than divided.
  //
  // A separator's row is shorter than a real one, so `room / kRowHeight` both
  // understates what fits and -- once the panel was sized from that count --
  // would leave a gap under a ruled menu's last item.
  //
  // The first row always goes in, however little room there is: a panel with one
  // row too tall for the window is wrong, but a panel with no rows at all is a
  // card with nothing in it and nothing to say why.
  const int cap = std::max(1, overlay.maxRows);
  const int first = std::clamp(overlay.rows.scroll, 0, std::max(0, static_cast<int>(indices.size()) - 1));
  // Measured with the cursor the settings card and the sidebar place their rows
  // with, so "how much of this list fits" is one piece of arithmetic in the
  // shell rather than three. The rows themselves are placed with a second
  // cursor below, once the panel's origin is known.
  RowCursor fit(0.0f, 0.0f, 0.0f);
  fit.stopAt(room);
  if(overlay.hasRows()) {
    for(std::size_t i = static_cast<std::size_t>(first);
        i < indices.size() && fit.placed() < static_cast<std::size_t>(cap); ++i) {
      const float step = rowHeight(overlay.items[static_cast<std::size_t>(indices[i])]);
      if(!fit.fits(step)) break;
      fit.place(step);
    }
  }
  const int rowsShown = static_cast<int>(fit.placed());
  const float listH = fit.height();
  // Rows the panel actually held, which is what scrolling has to agree with.
  overlay.rows.fitted(fit.placed());
  // The grid's own height, from how many rows the swatches fill.
  const int swatchRows = overlay.isGrid()
                           ? static_cast<int>((indices.size() + kSwatchColumns - 1) / kSwatchColumns)
                           : 0;
  const float gridH = swatchRows > 0
                        ? static_cast<float>(swatchRows) * (kSwatchCell + kSwatchGap) - kSwatchGap
                        : 0.0f;
  const float confirmH = overlay.hasConfirmButtons() ? kRowHeight + kPadding : 0.0f;
  const float hintH = hintProbe;

  // A grid asks for exactly the width its columns need, rather than being
  // stretched to whatever the caller guessed: a swatch grid with a ragged right
  // edge reads as a list of colours that failed to line up.
  //
  // *And* for whatever its hint needs, which is the wider of the two here: six
  // swatches come to 200px and "Enter choose   Esc cancel" to rather more, so a
  // panel sized to the grid alone had its own hint hanging out past its right
  // edge -- which is what "the cancel goes out of the popup" was.
  const float gridW = static_cast<float>(kSwatchColumns) * (kSwatchCell + kSwatchGap) - kSwatchGap;
  float asked = overlay.width;
  if(overlay.isGrid()) {
    const float hintW = overlay.hint.empty()
                          ? 0.0f
                          : static_cast<float>(text.width(overlay.hint, hintFace()));
    asked = std::max(gridW, hintW) + kPadding * 2.0f;
  }
  const float width = std::min(asked, static_cast<float>(windowWidth) - 40.0f);
  // A hint under a grid needs the gap a hint under a list does not: a list row
  // carries its own vertical padding and a swatch is a hard-edged block, so
  // without it the hint sat directly against the bottom row of colours.
  const float gridHintGap = gridH > 0.0f && hintH > 0.0f ? kPadding : 0.0f;
  const float height = kPadding * 2.0f + titleH + fieldH + listH + gridH + gridHintGap + confirmH + hintH;

  float x = 0.0f;
  float y = 0.0f;
  if(overlay.anchored) {
    x = cardInsideWindow(overlay.anchorX, width, static_cast<float>(windowWidth));
    y = cardInsideWindow(overlay.anchorY, height, static_cast<float>(windowHeight));
  } else {
    x = cardInsideWindow(std::round((static_cast<float>(windowWidth) - width) / 2.0f), width,
                         static_cast<float>(windowWidth));
    // Not `cardInsideWindow` on this axis: a panel taller than the window is
    // pinned to the top and allowed to run past the bottom, because the list
    // above was already sized to `room` and pulling the panel *up* from a
    // proportional top would move a list that fits. The inset needs no clamp
    // here either -- this floor is 60, and 60 is already past it.
    y = std::max(60.0f, static_cast<float>(windowHeight) * 0.18f);
  }
  layout.panel = {x, y, width, height};

  // The band spans the panel, so the title reads as chrome across the top of
  // the card rather than as text inset into it.
  if(titleH > 0.0f) layout.title = {x, y, width, titleH};
  float cursorY = y + titleH + kPadding;
  if(field) {
    layout.field = {x + kPadding, cursorY, width - kPadding * 2.0f, kFieldHeight};
    cursorY += fieldH;
  }
  if(overlay.isGrid()) {
    // Centred, because the panel is as wide as the *wider* of the grid and the
    // hint: left-aligned under a hint that outruns it, the grid would sit off
    // to one side of its own panel.
    const float gridX = std::round(x + (width - gridW) / 2.0f);
    for(std::size_t i = 0; i < indices.size(); ++i) {
      const auto column = static_cast<float>(i % kSwatchColumns);
      const auto gridRow = static_cast<float>(i / kSwatchColumns);
      layout.itemRects.push_back({gridX + column * (kSwatchCell + kSwatchGap),
                                  cursorY + gridRow * (kSwatchCell + kSwatchGap),
                                  kSwatchCell, kSwatchCell});
      layout.itemIndices.push_back(indices[i]);
    }
    cursorY += gridH;
    if(hintH > 0.0f) {
      layout.hint = {x + kPadding, y + height - kPadding - hintH, width - kPadding * 2.0f, hintH};
    }
    return layout;
  }
  RowCursor rows(x, width, cursorY);
  for(int taken = 0; taken < rowsShown; ++taken) {
    const std::size_t i = static_cast<std::size_t>(first + taken);
    const float step = rowHeight(overlay.items[static_cast<std::size_t>(indices[i])]);
    layout.itemRects.push_back(rows.place(step, kSpace2 - 2.0f));
    layout.itemIndices.push_back(indices[i]);
  }
  cursorY = rows.y();
  if(overlay.hasConfirmButtons()) {
    // The consequence, then the buttons. It used to be the other way round --
    // the buttons went here and the hint was drawn at the panel's foot -- so
    // "This cannot be undone." sat *below* the Delete button that could not be
    // undone, which is the one order in which nobody reads it in time.
    if(hintH > 0.0f) {
      layout.hint = {x + kPadding, cursorY, width - kPadding * 2.0f, hintH};
      cursorY += hintH;
    }
    const float confirmX = x + width - kPadding - kButtonWidth;
    layout.itemRects.push_back({confirmX - kSpace2 - kButtonWidth, cursorY, kButtonWidth, kRowHeight});
    layout.itemIndices.push_back(-2);  // cancel
    layout.itemRects.push_back({confirmX, cursorY, kButtonWidth, kRowHeight});
    layout.itemIndices.push_back(-1);  // confirm
  } else if(hintH > 0.0f) {
    layout.hint = {x + kPadding, y + height - kPadding - hintH, width - kPadding * 2.0f, hintH};
  }
  return layout;
}

void OverlayStack::moveHighlight(int delta) {
  Overlay* overlay = top();
  if(!overlay) return;
  const auto indices = visibleIndices(*overlay);
  if(indices.empty()) return;
  const int count = static_cast<int>(indices.size());
  int position = 0;
  for(int i = 0; i < count; ++i) {
    if(indices[static_cast<std::size_t>(i)] == overlay->highlighted) {
      position = i;
      break;
    }
  }
  const int step = delta >= 0 ? 1 : -1;
  // Section headings are listed as disabled items, so movement steps over them
  // rather than parking on a row that Enter would ignore. Bounded by the row
  // count, so a list of nothing but headings still terminates.
  //
  // `delta` is a distance, not just a direction: a grid moves a whole row at a
  // time when the arrow points up or down, and each of those cells has to be
  // stepped over one at a time or the skipping rule above cannot apply.
  for(int move = 0; move < std::max(1, std::abs(delta)); ++move) {
    for(int taken = 0; taken < count; ++taken) {
      position = (position + step % count + count) % count;
      if(selectable(overlay->items[static_cast<std::size_t>(indices[static_cast<std::size_t>(position)])])) break;
    }
  }
  overlay->highlighted = indices[static_cast<std::size_t>(position)];
  ensureHighlightVisible();
}

void OverlayStack::resetHighlight() {
  Overlay* overlay = top();
  if(!overlay) return;
  overlay->rows.rebase();
  const auto indices = visibleIndices(*overlay);
  overlay->highlighted = indices.empty() ? 0 : indices.front();
  for(const int index : indices) {
    if(!selectable(overlay->items[static_cast<std::size_t>(index)])) continue;
    overlay->highlighted = index;
    break;
  }
}

void OverlayStack::ensureHighlightVisible() {
  Overlay* overlay = top();
  if(!overlay) return;
  const auto& indices = visibleIndices(*overlay);
  const auto at = std::find(indices.begin(), indices.end(), overlay->highlighted);
  if(at == indices.end()) return;
  overlay->rows.reveal(static_cast<int>(at - indices.begin()));
  overlay->rows.clamp(indices.size());
}

bool OverlayStack::handleWheel(float dy) {
  Overlay* overlay = top();
  if(!overlay) return false;
  if(!overlay->hasRows()) return true;
  overlay->rows.scrollBy(-static_cast<int>(dy * 3.0f), visibleIndices(*overlay).size());
  return true;
}

std::optional<OverlayResult> OverlayStack::commit() {
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  OverlayResult result;
  result.overlayId = overlay->id;
  result.value = overlay->value.text();
  // A picker commits the same way a list does: both are "the highlighted item
  // is the answer", and the only difference between them is how they are laid
  // out. Leaving the picker out of this branch was how it came to have a grid
  // that could be pointed at and no way to choose anything on it.
  if(overlay->choosesAnItem()) {
    const auto indices = visibleIndices(*overlay);
    if(indices.empty()) return std::nullopt;
    int chosen = overlay->highlighted;
    if(std::find(indices.begin(), indices.end(), chosen) == indices.end()) chosen = indices.front();
    const auto& item = overlay->items[static_cast<std::size_t>(chosen)];
    if(!selectable(item)) return std::nullopt;
    result.itemId = item.id;
  } else if(overlay->hasConfirmButtons()) {
    result.itemId = "confirm";
  }
  close();
  return result;
}

// Shutting the top overlay, and what that answers with.
//
// Escape and a click outside are the same event as far as the stack is
// concerned, and both had this written out: read the flag, build the result,
// close, and hand the result back only if the overlay asked to hear about
// being dismissed. Two copies of a four-line sequence whose *order* matters --
// the result has to be built before `close()` destroys the overlay it reads.
std::optional<OverlayResult> OverlayStack::dismissTop() {
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  const bool report = overlay->reportDismissal;
  OverlayResult dismissal {overlay->id, {}, overlay->value.text()};
  close();
  if(report) return dismissal;
  return std::nullopt;
}

std::optional<OverlayResult> OverlayStack::handleKey(SDL_Keycode key, bool ctrl, bool shift, bool& handled) {
  handled = false;
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  handled = true;

  if(key == SDLK_ESCAPE) return dismissTop();
  if(key == SDLK_RETURN || key == SDLK_KP_ENTER) return commit();
  // In a grid, down is a row and right is a cell. A list has no left and right,
  // and its rows are one cell wide, so both come to the same thing there.
  const int rowStep = overlay->isGrid() ? kSwatchColumns : 1;
  if(key == SDLK_DOWN) {
    moveHighlight(rowStep);
    return std::nullopt;
  }
  if(key == SDLK_UP) {
    moveHighlight(-rowStep);
    return std::nullopt;
  }
  if(overlay->isGrid() && (key == SDLK_RIGHT || key == SDLK_LEFT)) {
    moveHighlight(key == SDLK_RIGHT ? 1 : -1);
    return std::nullopt;
  }
  if(key == SDLK_TAB) {
    moveHighlight(shift ? -1 : 1);
    return std::nullopt;
  }
  if(overlay->takesTypedText()) {
    // The field owns caret motion, selection, word jumps and undo; only a key
    // it declines falls through to be swallowed below.
    const auto handledBy = editor::applyKeyToField(overlay->value, key, ctrl, shift);
    if(handledBy == editor::FieldKeyResult::Changed) resetHighlight();
    if(handledBy != editor::FieldKeyResult::Ignored) return std::nullopt;
  }
  // Anything else is swallowed so it cannot leak into the editor behind.
  return std::nullopt;
}

bool OverlayStack::handleText(const char* input) {
  Overlay* overlay = top();
  if(!overlay || !input) return false;
  if(!overlay->takesTypedText()) return true;
  overlay->value.editor.insert(input);
  resetHighlight();
  return true;
}

OverlayCursor OverlayStack::cursorAt(float x, float y) const {
  if(stack_.empty()) return OverlayCursor::Outside;
  if(!contains(lastLayout_.panel, x, y)) return OverlayCursor::Outside;
  // The field first: it is drawn inside the panel and a row rect never overlaps
  // it, so the order only matters for reading.
  if(lastLayout_.field.w > 0.0f && contains(lastLayout_.field, x, y)) return OverlayCursor::Text;
  for(std::size_t i = 0; i < lastLayout_.itemRects.size(); ++i) {
    if(!contains(lastLayout_.itemRects[i], x, y)) continue;
    // A disabled row is not a control, and a cursor saying it is would be the
    // cursor lying about the one thing it is for.
    const int index = lastLayout_.itemIndices[i];
    if(index >= 0 && index < static_cast<int>(stack_.back().items.size()) &&
       !selectable(stack_.back().items[static_cast<std::size_t>(index)])) {
      return OverlayCursor::Panel;
    }
    return OverlayCursor::Pointer;
  }
  return OverlayCursor::Panel;
}

std::optional<OverlayResult> OverlayStack::handleClick(float x, float y, bool& handled) {
  handled = false;
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  handled = true;

  // A click outside dismisses, and does not fall through to what is behind.
  if(!contains(lastLayout_.panel, x, y)) return dismissTop();
  for(std::size_t i = 0; i < lastLayout_.itemRects.size(); ++i) {
    if(!contains(lastLayout_.itemRects[i], x, y)) continue;
    const int index = lastLayout_.itemIndices[i];
    if(index == -2) {
      close();
      return std::nullopt;
    }
    if(index == -1) {
      OverlayResult result;
      result.overlayId = overlay->id;
      result.itemId = "confirm";
      result.value = overlay->value.text();
      close();
      return result;
    }
    if(!selectable(overlay->items[static_cast<std::size_t>(index)])) return std::nullopt;
    overlay->highlighted = index;
    return commit();
  }
  return std::nullopt;
}

void OverlayStack::handleMotion(float x, float y) {
  mouseX_ = x;
  mouseY_ = y;
  Overlay* overlay = top();
  if(!overlay) return;
  for(std::size_t i = 0; i < lastLayout_.itemRects.size(); ++i) {
    if(!contains(lastLayout_.itemRects[i], x, y)) continue;
    const int index = lastLayout_.itemIndices[i];
    if(index >= 0 && selectable(overlay->items[static_cast<std::size_t>(index)])) overlay->highlighted = index;
    return;
  }
}

// The ring around the cell the keyboard is on: the cursor colour, and a second
// stroke in the panel's own ground just outside it so the ring reads as a ring
// against any swatch it lands on rather than merging into a light one.
void drawHighlightRing(SDL_Renderer* renderer, Rect rect) {
  stroke(renderer, rect, theme().cursor);
  stroke(renderer, {rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f},
         theme().overlayBackground);
}

void OverlayStack::draw(SDL_Renderer* renderer, TextRenderer& text, int windowWidth, int windowHeight) {
  // Not const: drawing the field settles its scroll offset, so the next frame
  // and the next hit-test agree with what was painted.
  Overlay* overlay = top();
  if(!overlay) return;

  // Dim whatever is behind so the overlay reads as the focused surface. The
  // renderer blends for its whole life, set once where it is created; this used
  // to turn blending on here and leave it on, which is how a shell that had
  // opened a palette painted differently from one that had not.
  // The palette's own backdrop role, rather than a black wash at a hand-picked
  // alpha per theme: the light theme wants a cooler, lighter dim than the dark
  // one, and picking two numbers here is how the two drifted apart from the
  // rest of the palette.
  // Not every overlay dims. An anchored menu costs one click to dismiss, and an
  // overlay that filters as you type into the note is showing a list *about* the
  // sentence behind it: see `Overlay::dimsBehind`.
  if(overlay->dimsBehind && !overlay->anchored) {
    fill(renderer, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
         theme().overlayBackdrop);
  }

  const auto layout = layoutFor(*overlay, text, windowWidth, windowHeight);
  lastLayout_ = layout;

  // The chrome face, like every other surface that is not the note. A palette
  // set in the text face reads as a document about commands rather than as a
  // list of them, and its accelerators -- which are keys, not words -- have to
  // line up in a column to be scanned at all.
  const TextStyle titleStyle = titleFace();
  const TextStyle bodyStyle = chromeStyle();
  const TextStyle hintStyle = hintFace();

  // The panel, with the header band a titled overlay wears. The title used to
  // be a line of text on the panel's own ground, which is a title that looks
  // like the first row of the list beneath it -- and on a Confirm, whose "list"
  // is two buttons, like a stray label above them.
  const Rect header = drawTitledCard(renderer, layout.panel, layout.title.h);
  if(header.h > 0.0f) {
    text.draw(ellipsizeToWidth(text, overlay->title,
                               static_cast<int>(header.w - kPadding * 2.0f), titleStyle),
              header.x + kPadding, textTop(header, text, titleStyle), theme().chromeText,
              titleStyle);
  }

  if(overlay->takesTypedText()) {
    drawTextFieldFrame(renderer, layout.field, true);
    // `textTop`, like every other centred line in the shell. These four sites
    // each centred by hand and none of them rounded, so the palette's text
    // landed on half pixels and its glyph stems smeared.
    // One painter, shared with the sidebar's search box and the settings card's
    // filter. The prompt used to draw its placeholder *instead of* its field,
    // so an empty prompt -- which every prompt is for its first keystroke --
    // had no insertion point at all.
    TextFieldPaint paint;
    paint.box = layout.field;
    paint.textY = textTop(layout.field, text, bodyStyle);
    paint.padX = kRowPadX;
    paint.insetY = 4.0f;
    paint.placeholder = overlay->placeholder;
    paint.focused = true;
    // The shell settles the blink once a frame so this caret and the page's
    // cannot blink out of step. See `OverlayStack::setCaretVisible`.
    paint.caretVisible = caretVisible_;
    drawTextFieldText(renderer, text, bodyStyle, overlay->value, paint);
  }

  if(overlay->isGrid()) {
    const bool glyphs = overlay->kind == OverlayKind::GlyphPicker;
    for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
      const auto rect = layout.itemRects[i];
      const int index = layout.itemIndices[i];
      if(index < 0 || index >= static_cast<int>(overlay->items.size())) continue;
      const bool highlighted = index == overlay->highlighted || contains(rect, mouseX_, mouseY_);
      const bool inForce = index == overlay->current;
      if(glyphs) {
        // A glyph cannot fill its cell the way a colour does, so the cell needs
        // a ground of its own or the grid is eleven marks floating on the
        // panel with no edges to point at. The mark takes the accent when it is
        // the one in force, so the answer to "which is it now" is the mark
        // itself rather than a tick sitting on top of one.
        fill(renderer, rect, inForce ? theme().selectionFill : theme().surfaceRaised);
        const SDL_Color ink = inForce ? theme().accent
                                      : (highlighted ? theme().textPrimary : theme().textSecondary);
        // The id of the "no icon" cell is empty, and drawNoteGlyph declines it,
        // which is exactly the fallback the sidebar takes for a note with none.
        const auto& id = overlay->items[static_cast<std::size_t>(index)].id;
        if(!drawNoteGlyph(renderer, id, rect, ink)) {
          fill(renderer, {rect.x + rect.w / 2.0f - 5.0f, rect.y + rect.h / 2.0f, 10.0f, 1.0f}, ink);
        }
        if(highlighted) drawHighlightRing(renderer, rect);
        continue;
      }
      // The swatch fills its cell, so the thing being chosen is the thing being
      // pointed at. A colour shown as a chip inside a row would be competing
      // with the row's own ground for what the eye reads as "this colour".
      fill(renderer, rect, tagSwatch(index));
      // Two marks, and they say different things: a tick for the colour the tag
      // already has, an outline for the one the pointer is on. A picker that
      // shows only one of them cannot answer "what is it now" and "what would
      // this do" at the same time, which is the only question being asked.
      if(inForce) {
        drawCheckGlyph(renderer, {rect.x + rect.w / 2.0f - 6.0f, rect.y + rect.h / 2.0f - 6.0f,
                                  12.0f, 12.0f},
                       theme().onAccent);
      }
      if(highlighted) drawHighlightRing(renderer, rect);
    }
    if(!layout.hint.w) return;
    text.draw(overlay->hint, layout.hint.x, layout.hint.y, theme().textMuted, hintStyle);
    return;
  }

  for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
    const auto rect = layout.itemRects[i];
    const int index = layout.itemIndices[i];
    if(index < 0) {
      const bool isConfirm = index == -1;
      const bool hot = contains(rect, mouseX_, mouseY_);
      // Confirm wears the destructive colour: every Confirm overlay in the shell
      // asks about a deletion, and a button that is about to delete something
      // should not look like the one beside it that will not.
      drawButton(renderer, text, rect, isConfirm ? overlay->confirmLabel : std::string("Cancel"),
                 true, hot, isConfirm ? ButtonTone::Destructive : ButtonTone::Neutral);
      continue;
    }

    const auto& item = overlay->items[static_cast<std::size_t>(index)];
    if(item.separator) {
      drawMenuSeparator(renderer, rect);
      continue;
    }
    // The same row a menu-bar popup draws. A palette row, a context-menu row
    // and a menu-bar row are the same object reached through two item tables,
    // and this is where the second one projects into it.
    MenuRow row;
    row.label = item.label;
    row.accelerator = item.shortcut;
    row.detail = item.detail;
    row.enabled = item.enabled;
    row.highlighted = index == overlay->highlighted || contains(rect, mouseX_, mouseY_);
    row.checked = item.checked;
    row.destructive = item.destructive;
    drawMenuRow(renderer, text, rect, row);
  }

  // A list taller than the panel says so, or the last visible row would read as
  // the end of the list.
  const auto& filtered = visibleIndices(*overlay);
  if(overlay->hasRows() && !layout.itemRects.empty() &&
     filtered.size() > layout.itemRects.size()) {
    // The shell's scrollbar, not a fifth private one. This drew its own track
    // and thumb at its own inset, its own width and its own minimum height,
    // with no border on the thumb -- so a list that scrolled in the command
    // palette did not look like a list that scrolled anywhere else.
    //
    // `drawVerticalScrollbar` works in pixels and the overlay scrolls in rows,
    // which is a pure scaling: at `pitch` pixels a row, `viewport.h` is the
    // rows on screen and `maxScroll` the rows off it, and both the visible
    // fraction and the thumb's travel come out identical to what was here.
    const float shown = static_cast<float>(layout.itemRects.size());
    const float pitch = (layout.itemRects.back().y + layout.itemRects.back().h - layout.itemRects.front().y) / shown;
    const Rect band {layout.panel.x, layout.itemRects.front().y - kScrollbarInset, layout.panel.w,
                     shown * pitch + kScrollbarInset * 2.0f};
    const int hidden = static_cast<int>(filtered.size() - layout.itemRects.size());
    drawVerticalScrollbar(renderer, band,
                          static_cast<int>(std::lround(static_cast<float>(overlay->rows.scroll) * pitch)),
                          static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
  }

  if(!overlay->hint.empty() && layout.hint.h > 0.0f) {
    text.draw(overlay->hint, layout.hint.x, layout.hint.y, theme().textMuted, hintStyle);
  }

  if(overlay->hasRows() && filtered.empty()) {
    // Where the first row would have been. It used to be placed a bare 24
    // pixels up from the panel's foot, which is where the hint is drawn -- so a
    // filterable palette with a hint wrote the two over each other the moment
    // the query matched nothing.
    text.draw("No matches", layout.panel.x + kPadding + kSpace1,
              overlay->takesTypedText() ? layout.field.y + layout.field.h + kPadding
                                  : layout.panel.y + kPadding,
              theme().textMuted, bodyStyle);
  }
}

}
