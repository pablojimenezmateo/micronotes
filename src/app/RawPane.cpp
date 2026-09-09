#include "app/RawPane.h"

#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"
#include "ui/Theme.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/ClipGuard.h"

#include <algorithm>
#include <string>
#include <vector>

namespace micronotes::app {

using ui::ClipGuard;
using ui::Rect;
using ui::TextRenderer;
using ui::contains;
using ui::drawVerticalScrollbar;
using ui::fill;
using ui::stroke;
using ui::theme;

static editor::MeasureText editorMeasure(TextRenderer& text) {
  return [&text](std::string_view value) {
    return text.width(value, false, true);
  };
}

// The note soft-wrapped to the pane's column, rewrapped only when something it
// depends on has moved.
//
// Keyed on the editor's revision rather than on a copy of the note, which is
// most of `TD-14`. The old key was the source text itself: 17.6 us per
// keystroke to copy a 200 KB note into it and 9.8 us per frame to compare
// against it, for a question the revision answers in one word. The text size
// is in the key as well -- the wrap width is a rect and does not move when the
// reader makes the text bigger, so at one width the pane used to keep wrapping
// the note to a font it was no longer drawn in.
//
// What is *not* fixed is the rewrap itself: it is still the whole note, because
// `editor::softWrap` has no incremental form. See `docs/tech-debt.md`, TD-14.
const std::vector<editor::SoftWrapRow>& editorRows(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect writing = editorWritingRect(rect);
  const RawRowsKey key {ui.editor.revision(), static_cast<int>(std::max(1.0f, writing.w - 20.0f)),
                        ui::textSize()};
  if(const auto* rows = ui.raw.rows.get(key)) return *rows;
  return ui.raw.rows.store(key, editor::softWrap(ui.editor.text(), key.wrapWidth, editorMeasure(text)));
}

Rect editorWritingRect(Rect editorRect) {
  return ui::pageRectIn(editorRect);
}

void drawFindHighlights(SDL_Renderer* renderer, TextRenderer& text, const UiRuntime& ui, const std::string& line, Rect writing, float y) {
  const std::string& needle = ui.fields.find.text();
  if(needle.empty()) return;
  std::size_t pos = line.find(needle);
  while(pos != std::string::npos) {
    const auto prefix = std::string_view(line.data(), pos);
    const float x = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
    const float w = static_cast<float>(std::max(6, text.width(needle, false, true)));
    if(x < writing.x + writing.w - 8) {
      fill(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().searchMatch);
      stroke(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().searchMatchActive);
    }
    pos = line.find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
}

std::size_t editorIndexAtPoint(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const Rect writing = editorWritingRect(rect);
  const int visibleLine = std::max(0, static_cast<int>((y - (writing.y + 12)) / static_cast<float>(lineHeight)));
  const int rowIndex = std::clamp(ui.raw.list.scroll() + visibleLine, 0, std::max(0, static_cast<int>(rows.size()) - 1));
  return editor::offsetForRowX(rows[static_cast<std::size_t>(rowIndex)], x - (writing.x + 12), editorMeasure(text));
}

void placeEditorCursor(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  ui.editor.moveCursor(editorIndexAtPoint(text, ui, rect, x, y));
  ui.revealEditorCursor = true;
}



void drawEditor(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().editorBackground);
  const Rect writing = editorWritingRect(rect);
  drawSurface(renderer, writing, theme().editorBackground, ui.focus == FocusArea::Editor ? theme().accent : theme().border);
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  const int cursorRow = editor::rowForOffset(rows, ui.editor.cursor());
  if(ui.revealEditorCursor) {
    ui.raw.list.reveal(static_cast<float>(cursorRow), static_cast<float>(cursorRow + 1),
                       static_cast<float>(maxLines));
  }
  ui.raw.visibleRows = maxLines;
  ui.raw.list.setContent(static_cast<float>(maxLines), static_cast<float>(rows.size()));
  ui.revealEditorCursor = false;
  {
    ClipGuard clip(renderer, {writing.x + 1, writing.y + 1, writing.w - 2, writing.h - 2});
    float y = writing.y + 12;
    for(int i = ui.raw.list.scroll(); i < static_cast<int>(rows.size()) && y < writing.y + writing.h - 12; ++i) {
      const auto& row = rows[static_cast<std::size_t>(i)];
      const auto& line = row.text;
      if(ui.editor.hasSelection()) {
        const auto selStart = std::max(ui.editor.selectionStart(), row.start);
        const auto selEnd = std::min(ui.editor.selectionEnd(), row.end);
        if(selStart < selEnd) {
          const auto before = std::string_view(line.data(), selStart - row.start);
          const auto selected = std::string_view(line.data() + (selStart - row.start), selEnd - selStart);
          const float sx = writing.x + 12 + static_cast<float>(text.width(before, false, true));
          const float sw = static_cast<float>(text.width(selected, false, true));
          fill(renderer, {sx, y - 2, std::min(sw, writing.x + writing.w - 8 - sx), static_cast<float>(lineHeight)}, theme().selectionFill);
        }
      }
      drawFindHighlights(renderer, text, ui, line, writing, y);
      text.draw(line.empty() ? " " : line, writing.x + 12, y, theme().textPrimary, false, true);
      y += lineHeight;
    }
    if(ui.focus == FocusArea::Editor && cursorRow >= ui.raw.list.scroll() && cursorRow < ui.raw.list.scroll() + maxLines) {
      std::string prefix;
      if(cursorRow >= 0 && cursorRow < static_cast<int>(rows.size())) {
        const auto& row = rows[static_cast<std::size_t>(cursorRow)];
        const auto cursorInRow = ui.editor.cursor() <= row.end ? ui.editor.cursor() - row.start : row.text.size();
        prefix = row.text.substr(0, std::min<std::size_t>(row.text.size(), cursorInRow));
      }
      const float cursorX = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
      const float cursorY = writing.y + 12 + static_cast<float>((cursorRow - ui.raw.list.scroll()) * lineHeight);
      // On the blink's on-phase, like every other caret in the shell. See
      // `ui::CaretBlink`.
      if(ui.caret.visible) {
        fill(renderer, {std::min(cursorX, writing.x + writing.w - 8), cursorY, 2, static_cast<float>(lineHeight - 2)}, theme().accent);
      }
    }
    if(ui.editor.text().empty()) text.draw("Start typing...", writing.x + 12, writing.y + 12, theme().textSecondary);
  }
  drawVerticalScrollbar(renderer, writing, ui.raw.list.scroll(), ui.raw.list.maxScroll());
}

}
