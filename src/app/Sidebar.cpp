#include "app/Sidebar.h"

#include "app/Chrome.h"
#include "app/SidebarModel.h"
#include "core/editor/SingleLineView.h"
#include "ui/Metrics.h"
#include "ui/SearchScope.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"
#include "ui/TreeModel.h"

#include <algorithm>
#include <string>

namespace micronotes::app {
namespace {

using ui::ClipGuard;
using ui::Rect;
using ui::TextRenderer;
using ui::drawDisclosure;
using ui::drawEmptyMessage;
using ui::drawSectionLabel;
using ui::drawSelection;
using ui::ellipsizeToWidth;
using ui::fill;
using ui::hLine;
using ui::stroke;
using ui::theme;

// The prefix on the search box. Named because its width sets where the field
// begins, so the string and the geometry cannot drift apart.
constexpr std::string_view kSearchLabel = "Find";

// Reserved at the trailing edge of a folder row for the note count, so a long
// folder name is ellipsized before it reaches the number rather than over it.
constexpr float kCountColumnWidth = 26.0f;

// Core measures text through a callback so it stays free of any font
// dependency; this binds it to the renderer actually drawing the field.
editor::TextWidthFn fieldMeasure(const TextRenderer& text) {
  return [&text](std::string_view run) { return text.width(run); };
}

// The search field on top of the navigation it filters.
static void drawSidebarSearch(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect search = searchBoxRect(rect);
  const SearchBoxParts parts = searchBoxParts(rect, text);
  const bool focused = ui.focus == FocusArea::Search;
  const ui::TextStyle style {};
  ui::drawRoundedSurface(renderer, search, theme().inputBg, focused ? theme().accent : theme().hairline,
                         ui::kRadiusSmall);
  ui.searchScopeToggle = parts.scope;
  ui.offerTooltip(ui.searchScopeToggle,
                  "Searching " + std::string(ui::searchScopeName(ui.searchScope)) + " - click to change");
  text.draw(kSearchLabel, parts.label.x, ui::textTop(parts.label, text, style),
            focused ? theme().accent : theme().dim, style);
  drawTextField(renderer, text, ui, ui.search, parts.field, focused, "Search all notes");
  ui::drawRoundedSurface(renderer, parts.scope, focused ? theme().accentSoft : theme().surface,
                         focused ? theme().accentDim : theme().hairline, ui::kRadiusSmall);
  const auto scopeLabel = ui::searchScopeLabel(ui.searchScope);
  text.draw(scopeLabel,
            std::round(parts.scope.x + (parts.scope.w - static_cast<float>(text.width(scopeLabel, style))) / 2.0f),
            ui::textTop(parts.scope, text, style), focused ? theme().accent : theme().muted, style);
}

// The face a search snippet is set in. Read by the draw and by the trim that
// keeps a match inside the column, which are two places that have to agree
// about it or the trim is measured against the wrong font.
static ui::TextStyle snippetTextStyle() {
  return {ui::FontFamily::Sans, false, false, ui::type().tiny};
}

// The one place with nothing to list says which nothing it is, because the way
// out of each is different.
static void drawSidebarEmpty(TextRenderer& text, UiRuntime& ui, Rect list) {
  const float x = list.x + ui::kSpace2;
  const float y = list.y + ui::kSpace2;
  const float width = list.w - ui::kSpace2 * 2.0f;
  if(!ui.state.hasLibrary()) {
    drawEmptyMessage(text, "No library", "Point micronotes at a folder of notes.",
                     x, y, width, ui::keysFor(ui::ActionId::Settings) + "  Settings");
  } else if(!ui.search.empty()) {
    drawEmptyMessage(text, "Nothing matches", "No note contains \"" + ui.search.text() + "\".",
                     x, y, width, "Esc  clear the search");
  } else if(!ui.state.selection().tag.empty()) {
    drawEmptyMessage(text, "No notes with this tag", "Nothing carries #" + ui.state.selection().tag + " any more.",
                     x, y, width, "click the tag again to clear the filter");
  } else {
    drawEmptyMessage(text, "No notes yet", "Notes here are plain .md files.",
                     x, y, width, ui::keysFor(ui::ActionId::NewNote) + "  write the first one");
  }
}


}

// The search field sits on top of the navigation it filters, inside the
// sidebar, because filtering and browsing are the same question asked two ways
// and they used to be in two panels either side of a divider.
Rect searchBoxRect(Rect sidebar) {
  return {sidebar.x + ui::kSpace3, sidebar.y + ui::kSpace3, sidebar.w - ui::kSpace3 * 2.0f,
          ui::kSidebarSearchHeight};
}

// What is inside the search box, left to right: the "Find" label, the field it
// prefixes, and the scope toggle at the far end.
//
// Laid out from the label's measured width rather than from the 46 and 86 that
// used to be written here. Those were the numbers "Find" happens to need at the
// medium text size; at the large size the label ran into the text beside it,
// which is the failure the whole box exists to make impossible -- the label says
// what the field is for, and a label touching its own field says nothing.
SearchBoxParts searchBoxParts(Rect sidebar, const TextRenderer& text) {
  const Rect box = searchBoxRect(sidebar);
  const ui::TextStyle style {};
  const float line = static_cast<float>(text.lineHeight(style));
  const float toggle = std::min(box.h - ui::kSpace2, std::max(20.0f, line + ui::kSpace1));

  SearchBoxParts parts;
  parts.label = {box.x + ui::kSpace3, box.y, static_cast<float>(text.width(kSearchLabel, style)), box.h};
  parts.scope = {box.x + box.w - toggle - ui::kSpace1, std::round(box.y + (box.h - toggle) / 2.0f), toggle, toggle};
  const float left = parts.label.x + parts.label.w + ui::kSpace2;
  parts.field = {left, std::round(box.y + (box.h - line) / 2.0f),
                 std::max(1.0f, parts.scope.x - ui::kSpace2 - left), line};
  return parts;
}

Rect searchTextRect(Rect sidebar, const TextRenderer& text) {
  return searchBoxParts(sidebar, text).field;
}

// Everything below the search field: the scrolling row list, which is the only
// thing buildSidebarRows() and sidebarRowAt() ever measure against.
Rect sidebarListRect(Rect sidebar) {
  const float top = sidebar.y + ui::kSidebarSearchBand;
  return {sidebar.x, top, sidebar.w, std::max(0.0f, sidebar.y + sidebar.h - top)};
}

// Paints a single-line field: selection band, then the text scrolled so the
// caret is visible, then the caret. The fields this replaces drew only the
// string, which is why they had no visible insertion point, no selection, and
// no way to reach text past the right edge.
void drawTextField(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui,
                          editor::TextField& field, Rect box, bool focused,
                          std::string_view placeholder) {
  const auto view = editor::layoutSingleLine(field.editor, box.w, field.scrollX, fieldMeasure(text));
  field.scrollX = view.scrollX;

  ClipGuard clip(renderer, box);
  const float originX = box.x - view.scrollX;
  if(focused && view.hasSelection) {
    fill(renderer, {originX + view.selectionStartX, box.y - 2.0f,
                    view.selectionEndX - view.selectionStartX, box.h + 4.0f}, theme().selectionBg);
  }
  if(field.empty() && !placeholder.empty()) {
    text.draw(placeholder, box.x, box.y, theme().dim);
  } else {
    text.draw(field.text(), originX, box.y, theme().text);
  }
  if(focused) {
    const float caretX = originX + view.caretX;
    fill(renderer, {caretX, box.y - 2.0f, 2.0f, box.h + 4.0f}, theme().accent);
    // Give the IME a candidate rectangle here too. Without it a dead-key or
    // composition popup opened while typing in a field lands at the window
    // origin instead of next to the text being composed.
    ui.caretReported = true;
    ui.caretRect = SDL_Rect {static_cast<int>(caretX), static_cast<int>(box.y - 2.0f),
                             2, static_cast<int>(box.h + 4.0f)};
  }
}

// Byte offset in `field` under a pointer at window x, for a field drawn in
// `box`. Always a code point boundary.
std::size_t fieldOffsetAtX(const TextRenderer& text, const editor::TextField& field, Rect box, float x) {
  return editor::offsetAtX(field.text(), x - box.x + field.scrollX, fieldMeasure(text));
}

void drawSidebar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().sidebarBg);
  ClipGuard clip(renderer, rect);
  drawSidebarSearch(renderer, text, ui, rect);

  const Rect list = sidebarListRect(rect);
  ui.sidebarRect = list;
  const ui::TextStyle rowStyle {ui::FontFamily::Sans, false, false, ui::type().ui};
  const ui::TextStyle snippetStyle = snippetTextStyle();
  // Measured here and handed to the model, which stays free of the font: the
  // rows have to be as tall as the text they hold, and only the renderer knows
  // how tall that is.
  const SidebarMetrics metrics = sidebarMetrics(text.lineHeight(rowStyle), text.lineHeight(snippetStyle));
  buildSidebarRows(ui, list, metrics);
  if(ui.sidebarRows.empty()) {
    drawSidebarEmpty(text, ui, list);
    return;
  }

  ClipGuard listClip(renderer, list);
  const auto& selection = ui.state.selection();
  const auto snippetWidth = [&](std::string_view value) {
    perf::addCounter(perf::CounterId::SidebarSnippetMeasures);
    return text.width(value, snippetStyle);
  };
  // The visible band, not the library: two binary searches over a row list that
  // tiles the panel. See `sidebarRowRange`.
  const auto [firstRow, lastRow] = sidebarRowRange(ui.sidebarRows, list.y, list.y + list.h);
  for(std::size_t i = firstRow; i < lastRow; ++i) {
    // Trimmed now that the row is known to be painted. See `fillSearchSnippets`.
    fillSearchSnippets(ui, i, list.w, snippetWidth);
    const auto& row = ui.sidebarRows[i];
    perf::addCounter(perf::CounterId::SidebarRowsDrawn);
    const bool hot = ui.hovered(row.rect);
    // Two columns, and every kind of row uses them: the gutter that holds a
    // disclosure or an icon, and the label beside it. Indent moves both.
    const float gutterX = row.rect.x + kSidebarGutterX + static_cast<float>(row.tree.depth) * kSidebarIndent;
    const float labelX = row.rect.x + kSidebarLabelX + static_cast<float>(row.tree.depth) * kSidebarIndent;

    if(row.kind == SidebarRow::Kind::SectionLabel) {
      // Sits on the label column of the rows it heads, and on the baseline the
      // row's own height gives rather than a fixed drop into it.
      drawSectionLabel(text, row.label, row.rect.x + kSidebarLabelX,
                       row.rect.y + row.rect.h - static_cast<float>(text.lineHeight(snippetStyle)) - ui::kSpace1);
      continue;
    }
    if(row.kind == SidebarRow::Kind::SearchResult) {
      const bool selected = row.noteId == selection.noteId;
      drawSelection(renderer, row.rect, selected, hot);
      if(!selected && !hot) {
        hLine(renderer, row.rect.x + ui::kSpace2, row.rect.x + row.rect.w - ui::kSpace2,
              row.rect.y + row.rect.h, theme().hairline);
      }
      const Rect titleRow {row.rect.x, row.rect.y, row.rect.w, metrics.resultTitle};
      text.draw(ellipsizeToWidth(text, row.title, static_cast<int>(row.rect.w - kSidebarLabelX - ui::kSpace2), rowStyle),
                row.rect.x + kSidebarLabelX, ui::textTop(titleRow, text, rowStyle),
                selected ? theme().text : theme().muted, rowStyle);
      float snippetY = row.rect.y + metrics.resultTitle;
      for(const auto& line : row.matchLines) {
        // The match, marked with the same fill find-in-note uses, so a match is
        // a match wherever it is shown. Drawn under the text rather than over
        // it, and the line is already trimmed to keep the span in view.
        const float snippetX = row.rect.x + kSidebarLabelX;
        if(line.length > 0) {
          const std::string_view shown = line.text;
          const float from = static_cast<float>(text.width(shown.substr(0, line.start), snippetStyle));
          const float to = static_cast<float>(text.width(shown.substr(0, line.start + line.length), snippetStyle));
          ui::fillRounded(renderer, {snippetX + from, snippetY,
                                     std::max(2.0f, to - from), metrics.snippet - 1.0f},
                          theme().findBg, ui::kRadiusSmall / 2.0f);
        }
        text.draw(line.text, snippetX, snippetY, selected ? theme().accent : theme().dim, snippetStyle);
        snippetY += metrics.snippet;
      }
      continue;
    }
    if(row.kind == SidebarRow::Kind::Tag) {
      const bool selected = selection.tag == row.tag;
      drawSelection(renderer, row.rect, selected, hot);
      text.draw("#" + ellipsizeToWidth(text, row.tag, static_cast<int>(row.rect.w - kSidebarLabelX - ui::kSpace4), rowStyle),
                row.rect.x + kSidebarLabelX, ui::textTop(row.rect, text, rowStyle),
                selected ? theme().accent : theme().dim, rowStyle);
      continue;
    }

    // Nesting guides: one hairline per level above this row, drawn under it
    // rather than beside it, so a run of siblings reads as one branch. Drawn
    // before the row's own fill would be wrong -- the fill is what a selected
    // row is -- so they go on top of it and stop short of the text.
    const bool isNote = row.tree.kind == ui::TreeRowKind::Note;
    // A folder is the current context and a note is the open document, so only
    // the note wears the strong selection: two accent bars at once would read
    // as two selections rather than one place and one file.
    const bool selected = isNote && row.tree.noteId == selection.noteId;
    const bool current = !isNote && selection.tag.empty() && selection.folder == row.tree.folder;
    const bool dropTarget = ui.sidebarDropRow && *ui.sidebarDropRow == i;
    if(current) fill(renderer, row.rect, theme().hoverBg);
    drawSelection(renderer, row.rect, selected, hot || dropTarget);
    if(dropTarget) stroke(renderer, row.rect, theme().accent);

    // Down the middle of the gutter each ancestor level would have used, so a
    // guide lines up with the disclosure of the folder it descends from.
    for(int depth = 0; depth < row.tree.depth; ++depth) {
      const float guideX = std::round(row.rect.x + kSidebarGutterX + kSidebarGutterWidth / 2.0f
                                      + static_cast<float>(depth) * kSidebarIndent);
      fill(renderer, {guideX, row.rect.y, ui::kTreeGuideWidth, row.rect.h}, theme().hairline);
    }

    if(row.disclosure.w > 0.0f) {
      drawDisclosure(renderer, row.disclosure, row.tree.expanded,
                     ui.hovered(row.disclosure) ? theme().text : theme().dim);
    }
    if(isNote) {
      drawNoteIcon(renderer, text, row.tree.icon,
                   {gutterX, row.rect.y + (row.rect.h - kSidebarGutterWidth) / 2.0f,
                    kSidebarGutterWidth, kSidebarGutterWidth},
                   selected ? theme().accent : theme().dim);
    }
    const float labelY = ui::textTop(row.rect, text, rowStyle);
    const float countW = row.tree.noteCount > 0 && !isNote ? kCountColumnWidth : ui::kSpace2;
    text.draw(ellipsizeToWidth(text, row.tree.label, static_cast<int>(row.rect.x + row.rect.w - labelX - countW), rowStyle),
              labelX, labelY, selected || current ? theme().text : (isNote ? theme().muted : theme().text), rowStyle);
    if(!isNote && row.tree.noteCount > 0) {
      const auto count = std::to_string(row.tree.noteCount);
      text.draw(count, row.rect.x + row.rect.w - static_cast<float>(text.width(count, rowStyle)) - ui::kSpace2,
                labelY, current ? theme().accent : theme().dim, rowStyle);
    }
  }
  drawVerticalScrollbar(renderer, list, ui.sidebarScroll, ui.sidebarMaxScroll);
}

}
