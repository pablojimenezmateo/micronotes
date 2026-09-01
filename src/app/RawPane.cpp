#include "app/RawPane.h"

#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Theme.h"

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

const std::vector<editor::SoftWrapRow>& editorRows(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect writing = editorWritingRect(rect);
  const int wrapWidth = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
  const auto& source = ui.editor.text();
  if(ui.cachedEditorRowsWidth != wrapWidth || ui.cachedEditorRowsSource != source) {
    ui.cachedEditorRowsWidth = wrapWidth;
    ui.cachedEditorRowsSource = source;
    ui.cachedEditorRows = editor::softWrap(ui.cachedEditorRowsSource, wrapWidth, editorMeasure(text));
  }
  return ui.cachedEditorRows;
}

int editorMaxScroll(TextRenderer& text, UiRuntime& ui, Rect rect) {
  Rect writing = editorWritingRect(rect);
  const int lineHeight = text.lineHeight();
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  ui.editorVisibleRows = maxLines;
  const int lineCount = static_cast<int>(editorRows(text, ui, rect).size());
  return std::max(0, lineCount - maxLines);
}


Rect editorWritingRect(Rect editorRect) {
  return {editorRect.x + 8.0f, editorRect.y + 8.0f, editorRect.w - 16.0f, editorRect.h - 28.0f};
}

void drawFindHighlights(SDL_Renderer* renderer, TextRenderer& text, const UiRuntime& ui, const std::string& line, Rect writing, float y) {
  const std::string& needle = ui.find.text();
  if(needle.empty()) return;
  std::size_t pos = line.find(needle);
  while(pos != std::string::npos) {
    const auto prefix = std::string_view(line.data(), pos);
    const float x = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
    const float w = static_cast<float>(std::max(6, text.width(needle, false, true)));
    if(x < writing.x + writing.w - 8) {
      fill(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().findBg);
      stroke(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().findBorder);
    }
    pos = line.find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
}

std::size_t editorIndexAtPoint(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const Rect writing = editorWritingRect(rect);
  const int visibleLine = std::max(0, static_cast<int>((y - (writing.y + 12)) / static_cast<float>(lineHeight)));
  const int rowIndex = std::clamp(ui.editorScroll + visibleLine, 0, std::max(0, static_cast<int>(rows.size()) - 1));
  return editor::offsetForRowX(rows[static_cast<std::size_t>(rowIndex)], x - (writing.x + 12), editorMeasure(text));
}

void placeEditorCursor(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  ui.editor.moveCursor(editorIndexAtPoint(text, ui, rect, x, y));
  ui.revealEditorCursor = true;
}



void drawEditor(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().editorBg);
  Rect writing {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  drawSurface(renderer, writing, theme().pageSurface, ui.focus == FocusArea::Editor ? theme().accentDim : theme().hairline);
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  const int cursorRow = editor::rowForOffset(rows, ui.editor.cursor());
  if(ui.revealEditorCursor) {
    if(cursorRow < ui.editorScroll) ui.editorScroll = cursorRow;
    if(cursorRow >= ui.editorScroll + maxLines) ui.editorScroll = cursorRow - maxLines + 1;
  }
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - maxLines);
  ui.editorScroll = std::clamp(ui.editorScroll, 0, maxScroll);
  ui.revealEditorCursor = false;
  {
    ClipGuard clip(renderer, {writing.x + 1, writing.y + 1, writing.w - 2, writing.h - 2});
    float y = writing.y + 12;
    for(int i = ui.editorScroll; i < static_cast<int>(rows.size()) && y < writing.y + writing.h - 12; ++i) {
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
          fill(renderer, {sx, y - 2, std::min(sw, writing.x + writing.w - 8 - sx), static_cast<float>(lineHeight)}, theme().selectionBg);
        }
      }
      drawFindHighlights(renderer, text, ui, line, writing, y);
      text.draw(line.empty() ? " " : line, writing.x + 12, y, theme().text, false, true);
      y += lineHeight;
    }
    if(ui.focus == FocusArea::Editor && cursorRow >= ui.editorScroll && cursorRow < ui.editorScroll + maxLines) {
      std::string prefix;
      if(cursorRow >= 0 && cursorRow < static_cast<int>(rows.size())) {
        const auto& row = rows[static_cast<std::size_t>(cursorRow)];
        const auto cursorInRow = ui.editor.cursor() <= row.end ? ui.editor.cursor() - row.start : row.text.size();
        prefix = row.text.substr(0, std::min<std::size_t>(row.text.size(), cursorInRow));
      }
      const float cursorX = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
      const float cursorY = writing.y + 12 + static_cast<float>((cursorRow - ui.editorScroll) * lineHeight);
      fill(renderer, {std::min(cursorX, writing.x + writing.w - 8), cursorY, 2, static_cast<float>(lineHeight - 2)}, theme().accent);
    }
    if(ui.editor.text().empty()) text.draw("Start typing...", writing.x + 12, writing.y + 12, theme().muted);
  }
  drawVerticalScrollbar(renderer, writing, ui.editorScroll, maxScroll);
}

}
