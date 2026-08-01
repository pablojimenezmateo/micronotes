#include "core/editor/SingleLineView.h"
#include "ui/Overlay.h"

#include "ui/Fuzzy.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

constexpr float kRowHeight = 34.0f;
constexpr float kFieldHeight = 36.0f;
constexpr float kPadding = 10.0f;

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
  overlay.highlighted = 0;
  overlay.scroll = 0;
  stack_.push_back(std::move(overlay));
  resetHighlight();
}

void OverlayStack::close() {
  if(!stack_.empty()) stack_.pop_back();
}

void OverlayStack::closeAll() {
  stack_.clear();
}

std::vector<int> OverlayStack::visibleIndices(const Overlay& overlay) const {
  std::vector<int> indices;
  if(!overlay.filterable || overlay.value.empty()) {
    for(int i = 0; i < static_cast<int>(overlay.items.size()); ++i) indices.push_back(i);
    return indices;
  }
  std::vector<std::pair<int, int>> scored;  // (score, index)
  for(int i = 0; i < static_cast<int>(overlay.items.size()); ++i) {
    const auto& item = overlay.items[static_cast<std::size_t>(i)];
    auto score = fuzzyScore(item.label, overlay.value.text());
    if(!score && !item.detail.empty()) score = fuzzyScore(item.detail, overlay.value.text());
    if(score) scored.emplace_back(*score, i);
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for(const auto& [_, index] : scored) indices.push_back(index);
  return indices;
}

OverlayStack::Layout OverlayStack::layoutFor(const Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const {
  Layout layout;
  const auto indices = visibleIndices(overlay);
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
  const float confirmH = overlay.kind == OverlayKind::Confirm ? kRowHeight + kPadding : 0.0f;
  const float hintH = overlay.hint.empty() ? 0.0f : static_cast<float>(text.lineHeight(TextStyle {FontFamily::Sans, false, false, type().tiny})) + 8.0f;

  const float width = std::min(overlay.width, static_cast<float>(windowWidth) - 40.0f);
  const float height = kPadding * 2.0f + titleH + fieldH + listH + confirmH + hintH;

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
  const int first = std::clamp(overlay.scroll, 0, std::max(0, static_cast<int>(indices.size()) - rowsShown));
  for(std::size_t i = static_cast<std::size_t>(first);
      i < indices.size() && static_cast<int>(i) - first < rowsShown; ++i) {
    layout.itemRects.push_back({x + 6.0f, cursorY, width - 12.0f, kRowHeight});
    layout.itemIndices.push_back(indices[i]);
    cursorY += kRowHeight;
  }
  if(overlay.kind == OverlayKind::Confirm) {
    layout.itemRects.push_back({x + width - 200.0f, cursorY, 92.0f, kRowHeight});
    layout.itemIndices.push_back(-2);  // cancel
    layout.itemRects.push_back({x + width - 102.0f, cursorY, 96.0f, kRowHeight});
    layout.itemIndices.push_back(-1);  // confirm
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
  if(overlay->kind == OverlayKind::List) {
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
    close();
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
    const auto handledBy = applyKeyToField(overlay->value, key, ctrl, shift);
    if(handledBy == FieldKeyResult::Changed) resetHighlight();
    if(handledBy != FieldKeyResult::Ignored) return std::nullopt;
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
    close();
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

  // Dim whatever is behind so the overlay reads as the focused surface.
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  fill(renderer, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
       SDL_Color {0, 0, 0, static_cast<Uint8>(themeMode() == ThemeMode::Dark ? 120 : 60)});

  const auto layout = layoutFor(*overlay, text, windowWidth, windowHeight);
  lastLayout_ = layout;

  drawSurface(renderer, layout.panel, theme().surfaceElevated, theme().divider);

  const TextStyle titleStyle {FontFamily::Sans, true, false, type().small};
  const TextStyle bodyStyle {FontFamily::Sans, false, false, type().ui};
  const TextStyle hintStyle {FontFamily::Sans, false, false, type().tiny};

  float y = layout.panel.y + kPadding;
  if(!overlay->title.empty()) {
    text.draw(overlay->title, layout.panel.x + kPadding, y, theme().muted, titleStyle);
    y += static_cast<float>(text.lineHeight(titleStyle)) + 8.0f;
  }

  if(usesField(*overlay)) {
    drawSurface(renderer, layout.field, theme().inputBg, theme().accentDim);
    const float textY = layout.field.y + (layout.field.h - static_cast<float>(text.lineHeight(bodyStyle))) / 2.0f;
    if(overlay->value.empty()) {
      text.draw(overlay->placeholder, layout.field.x + 10.0f, textY, theme().dim, bodyStyle);
    } else {
      // The field lays itself out: the view reports where the caret and the
      // selection sit after scrolling, so a long value keeps the caret in
      // sight instead of always pinning the end of the text.
      const float inner = layout.field.w - 20.0f;
      const auto measure = [&](std::string_view value) { return text.width(value, bodyStyle); };
      const auto view = editor::layoutSingleLine(overlay->value.editor, inner, overlay->value.scrollX, measure);
      overlay->value.scrollX = view.scrollX;
      const float left = layout.field.x + 10.0f - view.scrollX;
      ClipGuard clip(renderer, layout.field);
      if(view.hasSelection) {
        fill(renderer, {left + view.selectionStartX, layout.field.y + 6.0f,
                        view.selectionEndX - view.selectionStartX, layout.field.h - 12.0f},
             theme().selectionBg);
      }
      text.draw(overlay->value.text(), left, textY, theme().text, bodyStyle);
      fill(renderer, {left + view.caretX, layout.field.y + 8.0f, 2.0f, layout.field.h - 16.0f}, theme().accent);
    }
  }

  for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
    const auto rect = layout.itemRects[i];
    const int index = layout.itemIndices[i];
    if(index < 0) {
      const bool isConfirm = index == -1;
      const bool hot = contains(rect, mouseX_, mouseY_);
      const SDL_Color face = isConfirm ? (overlay->items.empty() ? theme().warn : theme().warn) : theme().surface;
      fill(renderer, rect, isConfirm ? face : (hot ? theme().hoverBg : theme().surface));
      stroke(renderer, rect, isConfirm ? face : theme().hairline);
      const auto label = isConfirm ? overlay->confirmLabel : std::string("Cancel");
      const int labelW = text.width(label, bodyStyle);
      text.draw(label, rect.x + (rect.w - static_cast<float>(labelW)) / 2.0f,
                rect.y + (rect.h - static_cast<float>(text.lineHeight(bodyStyle))) / 2.0f,
                isConfirm ? theme().onAccent : theme().text, bodyStyle);
      continue;
    }

    const auto& item = overlay->items[static_cast<std::size_t>(index)];
    const bool selected = index == overlay->highlighted;
    if(selected) {
      fill(renderer, rect, theme().selectedBg);
    } else if(contains(rect, mouseX_, mouseY_) && item.enabled) {
      fill(renderer, rect, theme().hoverBg);
    }
    const SDL_Color label = !item.enabled ? theme().dim : (item.destructive ? theme().warn : theme().text);
    const float labelY = rect.y + (rect.h - static_cast<float>(text.lineHeight(bodyStyle))) / 2.0f;
    float labelX = rect.x + 10.0f;
    int available = static_cast<int>(rect.w - 20.0f);
    // Both trailing pieces are laid out right to left against a running edge:
    // a fixed gap between them only works while the shortcut is short, and a
    // deletion timestamp is not.
    float right = rect.x + rect.w - 10.0f;
    const float hintY = rect.y + (rect.h - static_cast<float>(text.lineHeight(hintStyle))) / 2.0f;
    if(!item.shortcut.empty()) {
      const int shortcutW = text.width(item.shortcut, hintStyle);
      text.draw(item.shortcut, right - static_cast<float>(shortcutW), hintY, theme().dim, hintStyle);
      right -= static_cast<float>(shortcutW) + 12.0f;
      available -= shortcutW + 12;
    }
    if(!item.detail.empty()) {
      const auto detail = ellipsizeToWidth(text, item.detail, available / 2, hintStyle);
      const int detailW = text.width(detail, hintStyle);
      text.draw(detail, right - static_cast<float>(detailW), hintY, theme().dim, hintStyle);
      right -= static_cast<float>(detailW) + 12.0f;
      available -= detailW + 12;
    }
    text.draw(ellipsizeToWidth(text, item.label, available, bodyStyle), labelX, labelY, label, bodyStyle);
  }

  // A list taller than the panel says so, or the last visible row would read as
  // the end of the list.
  const auto filtered = visibleIndices(*overlay);
  if(overlay->kind == OverlayKind::List && !layout.itemRects.empty() &&
     filtered.size() > layout.itemRects.size()) {
    const Rect firstRow = layout.itemRects.front();
    const Rect lastRow = layout.itemRects.back();
    const float top = firstRow.y;
    const float height = lastRow.y + lastRow.h - top;
    const float shown = static_cast<float>(layout.itemRects.size());
    const float thumbH = std::max(18.0f, height * shown / static_cast<float>(filtered.size()));
    const float hidden = static_cast<float>(filtered.size()) - shown;
    const float at = std::clamp(static_cast<float>(overlay->scroll) / hidden, 0.0f, 1.0f);
    const float x = layout.panel.x + layout.panel.w - 6.0f;
    fill(renderer, {x, top, 3.0f, height}, theme().scrollTrack);
    fill(renderer, {x, top + (height - thumbH) * at, 3.0f, thumbH}, theme().scrollThumb);
  }

  if(!overlay->hint.empty()) {
    text.draw(overlay->hint, layout.panel.x + kPadding,
              layout.panel.y + layout.panel.h - kPadding - static_cast<float>(text.lineHeight(hintStyle)),
              theme().dim, hintStyle);
  }

  if(overlay->kind == OverlayKind::List && filtered.empty()) {
    text.draw("No matches", layout.panel.x + kPadding + 4.0f, layout.panel.y + layout.panel.h - kPadding - 24.0f, theme().dim, bodyStyle);
  }
}

}
