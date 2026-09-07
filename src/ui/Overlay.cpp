#include "core/editor/SingleLineView.h"
#include "ui/Overlay.h"

#include "core/perf/PerformanceCounters.h"

#include "ui/Fuzzy.h"
#include "ui/Metrics.h"
#include "ui/TagColors.h"

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
// Inside a row, and between the two trailing pieces of one: the shortcut and
// the detail. Written out as 10, 20 and 12 at seven sites.
constexpr float kRowPadX = kPadding;
constexpr float kRowGap = kSpace3;
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

bool usesField(const Overlay& overlay) {
  return overlay.kind == OverlayKind::TextPrompt || (overlay.kind == OverlayKind::List && overlay.filterable);
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
  overlay.scroll = 0;
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

void OverlayStack::closeAll() {
  stack_.clear();
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
    auto score = fuzzyScore(item.label, query);
    if(!score && !item.detail.empty()) score = fuzzyScore(item.detail, query);
    if(score) scored.emplace_back(*score, i);
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  overlay.filterCache.reserve(scored.size());
  for(const auto& [_, index] : scored) overlay.filterCache.push_back(index);
  return overlay.filterCache;
}

OverlayStack::Layout OverlayStack::layoutFor(const Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const {
  Layout layout;
  // A reference into the standing answer: the layout only reads it, and nothing
  // in here changes the query it is keyed on. On a "Go to note" palette over a
  // large library this is the difference between one vector and a copy of it per
  // frame.
  const std::vector<int>& indices = visibleIndices(overlay);
  const bool field = usesField(overlay);

  const float titleH = overlay.title.empty() ? 0.0f : static_cast<float>(text.lineHeight(TextStyle {FontFamily::Sans, true, false, type().small})) + 8.0f;
  const float fieldH = field ? kFieldHeight + kPadding : 0.0f;
  const float hintProbe = overlay.hint.empty() ? 0.0f : static_cast<float>(text.lineHeight(TextStyle {FontFamily::Sans, false, false, type().tiny})) + 8.0f;
  // What is left of the window below where the panel starts, once its own
  // chrome is paid for. Measured from the top the panel will actually take, or
  // a tall list would be laid out past the bottom of the window.
  const float panelTop = overlay.anchored ? 8.0f : std::max(60.0f, static_cast<float>(windowHeight) * 0.18f);
  const float room = static_cast<float>(windowHeight) - panelTop - 8.0f
                   - titleH - (field ? kFieldHeight + kPadding : 0.0f) - hintProbe - kPadding * 2.0f;
  const int fits = std::max(3, static_cast<int>(room / kRowHeight));
  const int rowsShown = std::min({std::max(1, overlay.maxRows), fits, static_cast<int>(indices.size())});
  lastRows_ = std::min(std::max(1, overlay.maxRows), fits);
  const float rows = static_cast<float>(rowsShown);
  const float listH = overlay.kind == OverlayKind::List ? rows * kRowHeight : 0.0f;
  // The grid's own height, from how many rows the swatches fill.
  const int swatchRows = overlay.kind == OverlayKind::ColorPicker
                           ? static_cast<int>((indices.size() + kSwatchColumns - 1) / kSwatchColumns)
                           : 0;
  const float gridH = swatchRows > 0
                        ? static_cast<float>(swatchRows) * (kSwatchCell + kSwatchGap) - kSwatchGap
                        : 0.0f;
  const float confirmH = overlay.kind == OverlayKind::Confirm ? kRowHeight + kPadding : 0.0f;
  const float hintH = overlay.hint.empty() ? 0.0f : static_cast<float>(text.lineHeight(TextStyle {FontFamily::Sans, false, false, type().tiny})) + 8.0f;

  // A grid asks for exactly the width its columns need, rather than being
  // stretched to whatever the caller guessed: a swatch grid with a ragged right
  // edge reads as a list of colours that failed to line up.
  const float gridW = static_cast<float>(kSwatchColumns) * (kSwatchCell + kSwatchGap) - kSwatchGap;
  const float asked = overlay.kind == OverlayKind::ColorPicker ? gridW + kPadding * 2.0f
                                                               : overlay.width;
  const float width = std::min(asked, static_cast<float>(windowWidth) - 40.0f);
  const float height = kPadding * 2.0f + titleH + fieldH + listH + gridH + confirmH + hintH;

  float x = 0.0f;
  float y = 0.0f;
  if(overlay.anchored) {
    x = std::min(overlay.anchorX, static_cast<float>(windowWidth) - width - 8.0f);
    y = std::min(overlay.anchorY, static_cast<float>(windowHeight) - height - 8.0f);
  } else {
    x = std::round((static_cast<float>(windowWidth) - width) / 2.0f);
    y = std::max(60.0f, static_cast<float>(windowHeight) * 0.18f);
  }
  x = std::max(8.0f, x);
  y = std::max(8.0f, y);
  layout.panel = {x, y, width, height};

  float cursorY = y + kPadding + titleH;
  if(field) {
    layout.field = {x + kPadding, cursorY, width - kPadding * 2.0f, kFieldHeight};
    cursorY += fieldH;
  }
  if(overlay.kind == OverlayKind::ColorPicker) {
    for(std::size_t i = 0; i < indices.size(); ++i) {
      const auto column = static_cast<float>(i % kSwatchColumns);
      const auto gridRow = static_cast<float>(i / kSwatchColumns);
      layout.itemRects.push_back({x + kPadding + column * (kSwatchCell + kSwatchGap),
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
  const int first = std::clamp(overlay.scroll, 0, std::max(0, static_cast<int>(indices.size()) - rowsShown));
  for(std::size_t i = static_cast<std::size_t>(first);
      i < indices.size() && static_cast<int>(i) - first < rowsShown; ++i) {
    layout.itemRects.push_back({x + kSpace2 - 2.0f, cursorY, width - (kSpace2 - 2.0f) * 2.0f, kRowHeight});
    layout.itemIndices.push_back(indices[i]);
    cursorY += kRowHeight;
  }
  if(overlay.kind == OverlayKind::Confirm) {
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
  for(int taken = 0; taken < count; ++taken) {
    position = (position + step % count + count) % count;
    if(overlay->items[static_cast<std::size_t>(indices[static_cast<std::size_t>(position)])].enabled) break;
  }
  overlay->highlighted = indices[static_cast<std::size_t>(position)];
  ensureHighlightVisible();
}

void OverlayStack::resetHighlight() {
  Overlay* overlay = top();
  if(!overlay) return;
  overlay->scroll = 0;
  const auto indices = visibleIndices(*overlay);
  overlay->highlighted = indices.empty() ? 0 : indices.front();
  for(const int index : indices) {
    if(!overlay->items[static_cast<std::size_t>(index)].enabled) continue;
    overlay->highlighted = index;
    break;
  }
}

void OverlayStack::ensureHighlightVisible() {
  Overlay* overlay = top();
  if(!overlay) return;
  const auto indices = visibleIndices(*overlay);
  const int rows = std::max(1, lastRows_);
  const int count = static_cast<int>(indices.size());
  int position = -1;
  for(int i = 0; i < count; ++i) {
    if(indices[static_cast<std::size_t>(i)] != overlay->highlighted) continue;
    position = i;
    break;
  }
  if(position < 0) return;
  overlay->scroll = std::clamp(overlay->scroll, position - rows + 1, position);
  overlay->scroll = std::clamp(overlay->scroll, 0, std::max(0, count - rows));
}

bool OverlayStack::handleWheel(float dy) {
  Overlay* overlay = top();
  if(!overlay) return false;
  if(overlay->kind != OverlayKind::List) return true;
  const int count = static_cast<int>(visibleIndices(*overlay).size());
  overlay->scroll = std::clamp(overlay->scroll - static_cast<int>(dy * 3.0f), 0,
                               std::max(0, count - std::max(1, lastRows_)));
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
  if(overlay->kind == OverlayKind::List || overlay->kind == OverlayKind::ColorPicker) {
    const auto indices = visibleIndices(*overlay);
    if(indices.empty()) return std::nullopt;
    int chosen = overlay->highlighted;
    if(std::find(indices.begin(), indices.end(), chosen) == indices.end()) chosen = indices.front();
    const auto& item = overlay->items[static_cast<std::size_t>(chosen)];
    if(!item.enabled) return std::nullopt;
    result.itemId = item.id;
  } else if(overlay->kind == OverlayKind::Confirm) {
    result.itemId = "confirm";
  }
  close();
  return result;
}

std::optional<OverlayResult> OverlayStack::handleKey(SDL_Keycode key, bool ctrl, bool shift, bool& handled) {
  handled = false;
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  handled = true;

  if(key == SDLK_ESCAPE) {
    const bool report = overlay->reportDismissal;
    OverlayResult dismissal {overlay->id, {}, overlay->value.text()};
    close();
    if(report) return dismissal;
    return std::nullopt;
  }
  if(key == SDLK_RETURN || key == SDLK_KP_ENTER) return commit();
  if(key == SDLK_DOWN) {
    moveHighlight(1);
    return std::nullopt;
  }
  if(key == SDLK_UP) {
    moveHighlight(-1);
    return std::nullopt;
  }
  if(key == SDLK_TAB) {
    moveHighlight(shift ? -1 : 1);
    return std::nullopt;
  }
  if(usesField(*overlay)) {
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
  if(!usesField(*overlay)) return true;
  overlay->value.editor.insert(input);
  resetHighlight();
  return true;
}

std::optional<OverlayResult> OverlayStack::handleClick(float x, float y, bool& handled) {
  handled = false;
  Overlay* overlay = top();
  if(!overlay) return std::nullopt;
  handled = true;

  if(!contains(lastLayout_.panel, x, y)) {
    // A click outside dismisses, and does not fall through to what is behind.
    const bool report = overlay->reportDismissal;
    OverlayResult dismissal {overlay->id, {}, overlay->value.text()};
    close();
    if(report) return dismissal;
    return std::nullopt;
  }
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
    if(!overlay->items[static_cast<std::size_t>(index)].enabled) return std::nullopt;
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
    if(index >= 0 && overlay->items[static_cast<std::size_t>(index)].enabled) overlay->highlighted = index;
    return;
  }
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
  fill(renderer, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
       theme().overlayBackdrop);

  const auto layout = layoutFor(*overlay, text, windowWidth, windowHeight);
  lastLayout_ = layout;

  drawSurface(renderer, layout.panel, theme().overlayBackground, theme().border);

  // The chrome face, like every other surface that is not the note. A palette
  // set in the text face reads as a document about commands rather than as a
  // list of them, and its accelerators -- which are keys, not words -- have to
  // line up in a column to be scanned at all.
  const TextStyle titleStyle {FontFamily::Mono, true, false, type().chrome};
  const TextStyle bodyStyle = chromeStyle();
  const TextStyle hintStyle = chromeSmallStyle();

  float y = layout.panel.y + kPadding;
  if(!overlay->title.empty()) {
    text.draw(overlay->title, layout.panel.x + kPadding, y, theme().textSecondary, titleStyle);
    y += static_cast<float>(text.lineHeight(titleStyle)) + 8.0f;
  }

  if(usesField(*overlay)) {
    drawTextFieldFrame(renderer, layout.field, true);
    // `textTop`, like every other centred line in the shell. These four sites
    // each centred by hand and none of them rounded, so the palette's text
    // landed on half pixels and its glyph stems smeared.
    const float textY = textTop(layout.field, text, bodyStyle);
    if(overlay->value.empty()) {
      text.draw(overlay->placeholder, layout.field.x + kRowPadX, textY, theme().textMuted, bodyStyle);
    } else {
      // The field lays itself out: the view reports where the caret and the
      // selection sit after scrolling, so a long value keeps the caret in
      // sight instead of always pinning the end of the text.
      const float inner = layout.field.w - kRowPadX * 2.0f;
      const auto measure = [&](std::string_view value) { return text.width(value, bodyStyle); };
      const auto view = editor::layoutSingleLine(overlay->value.editor, inner, overlay->value.scrollX, measure);
      overlay->value.scrollX = view.scrollX;
      const float left = layout.field.x + kRowPadX - view.scrollX;
      ClipGuard clip(renderer, layout.field);
      if(view.hasSelection) {
        fill(renderer, {left + view.selectionStartX, layout.field.y + 3.0f,
                        view.selectionEndX - view.selectionStartX, layout.field.h - 6.0f},
             theme().selectionFill);
      }
      text.draw(overlay->value.text(), left, textY, theme().textPrimary, bodyStyle);
      fill(renderer, {left + view.caretX, layout.field.y + 4.0f, 2.0f, layout.field.h - 8.0f},
           theme().accent);
    }
  }

  if(overlay->kind == OverlayKind::ColorPicker) {
    for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
      const auto rect = layout.itemRects[i];
      const int index = layout.itemIndices[i];
      if(index < 0 || index >= static_cast<int>(overlay->items.size())) continue;
      const bool highlighted = index == overlay->highlighted || contains(rect, mouseX_, mouseY_);
      const bool inForce = index == overlay->current;
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
      if(highlighted) {
        stroke(renderer, rect, theme().cursor);
        stroke(renderer, {rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f},
               theme().overlayBackground);
      }
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
    const bool selected = index == overlay->highlighted;
    if(selected) {
      fill(renderer, rect, theme().rowHighlight);
    } else if(contains(rect, mouseX_, mouseY_) && item.enabled) {
      fill(renderer, rect, theme().rowHighlight);
    }
    const SDL_Color label = !item.enabled ? theme().textDisabled
                          : item.destructive ? theme().warn
                          : selected ? theme().textPrimary
                                     : theme().textSecondary;
    const float labelY = textTop(rect, text, bodyStyle);
    // The label starts where a menu row's does, so a palette row and a menu row
    // for the same command put their text in the same column.
    const float labelX = rect.x + kMenuPopupLabelInset;
    int available = static_cast<int>(rect.w - kMenuPopupLabelInset - kMenuPopupAcceleratorInset);
    // Both trailing pieces are laid out right to left against a running edge:
    // a fixed gap between them only works while the shortcut is short, and a
    // deletion timestamp is not.
    float right = rect.x + rect.w - kMenuPopupAcceleratorInset;
    const float hintY = textTop(rect, text, hintStyle);
    if(!item.shortcut.empty()) {
      const int shortcutW = text.width(item.shortcut, hintStyle);
      text.draw(item.shortcut, right - static_cast<float>(shortcutW), hintY,
                item.enabled ? theme().textMuted : theme().textDisabled, hintStyle);
      right -= static_cast<float>(shortcutW) + kRowGap;
      available -= shortcutW + static_cast<int>(kRowGap);
    }
    if(!item.detail.empty()) {
      const auto detail = ellipsizeToWidth(text, item.detail, available / 2, hintStyle);
      const int detailW = text.width(detail, hintStyle);
      text.draw(detail, right - static_cast<float>(detailW), hintY, theme().textMuted, hintStyle);
      right -= static_cast<float>(detailW) + kRowGap;
      available -= detailW + static_cast<int>(kRowGap);
    }
    text.draw(ellipsizeToWidth(text, item.label, available, bodyStyle), labelX, labelY, label, bodyStyle);
  }

  // A list taller than the panel says so, or the last visible row would read as
  // the end of the list.
  const auto& filtered = visibleIndices(*overlay);
  if(overlay->kind == OverlayKind::List && !layout.itemRects.empty() &&
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
    drawVerticalScrollbar(renderer, band, static_cast<int>(std::lround(static_cast<float>(overlay->scroll) * pitch)),
                          static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
  }

  if(!overlay->hint.empty() && layout.hint.h > 0.0f) {
    text.draw(overlay->hint, layout.hint.x, layout.hint.y, theme().textMuted, hintStyle);
  }

  if(overlay->kind == OverlayKind::List && filtered.empty()) {
    // Where the first row would have been. It used to be placed a bare 24
    // pixels up from the panel's foot, which is where the hint is drawn -- so a
    // filterable palette with a hint wrote the two over each other the moment
    // the query matched nothing.
    text.draw("No matches", layout.panel.x + kPadding + kSpace1,
              usesField(*overlay) ? layout.field.y + layout.field.h + kPadding
                                  : layout.panel.y + kPadding,
              theme().textMuted, bodyStyle);
  }
}

}
