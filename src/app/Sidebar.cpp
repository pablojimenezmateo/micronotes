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

// Core measures text through a callback so it stays free of any font
// dependency; this binds it to the renderer actually drawing the field.
editor::TextWidthFn fieldMeasure(const TextRenderer& text) {
  return [&text](std::string_view run) { return text.width(run); };
}

// The search field on top of the navigation it filters.
static void drawSidebarSearch(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect search = searchBoxRect(rect);
  const bool focused = ui.focus == FocusArea::Search;
  ui::drawRoundedSurface(renderer, search, theme().inputBg, focused ? theme().accent : theme().hairline,
                         ui::kRadiusSmall);
  ui.searchScopeToggle = {search.x + search.w - 30.0f, search.y + 5.0f, 24.0f, 24.0f};
  ui.offerTooltip(ui.searchScopeToggle,
                  "Searching " + std::string(ui::searchScopeName(ui.searchScope)) + " - click to change");
  text.draw("Find", search.x + 10.0f, search.y + 8.0f, focused ? theme().accent : theme().dim);
  drawTextField(renderer, text, ui, ui.search, searchTextRect(rect, text), focused, "Search all notes");
  ui::drawRoundedSurface(renderer, ui.searchScopeToggle, focused ? theme().accentSoft : theme().surface,
                         focused ? theme().accentDim : theme().hairline, ui::kRadiusSmall);
  text.draw(ui::searchScopeLabel(ui.searchScope), ui.searchScopeToggle.x + 8.0f, ui.searchScopeToggle.y + 4.0f,
            focused ? theme().accent : theme().muted);
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
  const Rect where {list.x + 8.0f, list.y + 8.0f, list.w - 16.0f, 120.0f};
  if(!ui.state.hasLibrary()) {
    drawEmptyMessage(text, "No library", "Point micronotes at a folder of notes.",
                     where, ui::keysFor(ui::ActionId::Settings) + "  Settings");
  } else if(!ui.search.empty()) {
    drawEmptyMessage(text, "Nothing matches", "No note contains \"" + ui.search.text() + "\".",
                     where, "Esc  clear the search");
  } else if(!ui.state.selection().tag.empty()) {
    drawEmptyMessage(text, "No notes with this tag", "Nothing carries #" + ui.state.selection().tag + " any more.",
                     where, "click the tag again to clear the filter");
  } else {
    drawEmptyMessage(text, "No notes yet", "Notes here are plain .md files.",
                     where, ui::keysFor(ui::ActionId::NewNote) + "  write the first one");
  }
}


}

// The search field sits on top of the navigation it filters, inside the
// sidebar, because filtering and browsing are the same question asked two ways
// and they used to be in two panels either side of a divider.
Rect searchBoxRect(Rect sidebar) {
  return {sidebar.x + 12.0f, sidebar.y + 12.0f, sidebar.w - 24.0f, ui::kSidebarSearchHeight};
}

// The strip inside the search box that holds the text: after the "Find" label
// and before the scope toggle. Layout and hit testing both derive from this, so
// a click lands where the glyph it pointed at is actually drawn.
Rect searchTextRect(Rect sidebar, const TextRenderer& text) {
  const Rect box = searchBoxRect(sidebar);
  return {box.x + 46.0f, box.y + 7.0f, box.w - 86.0f, static_cast<float>(text.lineHeight())};
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
  buildSidebarRows(ui, list);
  if(ui.sidebarRows.empty()) {
    drawSidebarEmpty(text, ui, list);
    return;
  }

  ClipGuard listClip(renderer, list);
  const auto& selection = ui.state.selection();
  const ui::TextStyle rowStyle {ui::FontFamily::Sans, false, false, ui::type().ui};
  const ui::TextStyle snippetStyle = snippetTextStyle();
  const auto snippetWidth = [&](std::string_view value) {
    return text.width(value, snippetStyle);
  };
  for(std::size_t i = 0; i < ui.sidebarRows.size(); ++i) {
    if(ui.sidebarRows[i].rect.y + ui.sidebarRows[i].rect.h < list.y ||
       ui.sidebarRows[i].rect.y > list.y + list.h) {
      continue;
    }
    // Trimmed now that the row is known to be painted. See `fillSearchSnippets`.
    fillSearchSnippets(ui, i, list.w, snippetWidth);
    const auto& row = ui.sidebarRows[i];
    perf::addCounter(perf::CounterId::SidebarRowsDrawn);
    const bool hot = ui.hovered(row.rect);
    const float indent = list.x + 10.0f + static_cast<float>(row.tree.depth) * kSidebarIndent;

    if(row.kind == SidebarRow::Kind::SectionLabel) {
      drawSectionLabel(text, row.label, list.x + 18, row.rect.y + 12);
      continue;
    }
    if(row.kind == SidebarRow::Kind::SearchResult) {
      const bool selected = row.noteId == selection.noteId;
      drawSelection(renderer, row.rect, selected, hot);
      if(!selected && !hot) hLine(renderer, row.rect.x + 8, row.rect.x + row.rect.w - 8, row.rect.y + row.rect.h, theme().hairline);
      text.draw(ellipsizeToWidth(text, row.title, static_cast<int>(row.rect.w - 24), rowStyle),
                row.rect.x + 12, row.rect.y + 3, selected ? theme().text : theme().muted, rowStyle);
      float snippetY = row.rect.y + kSidebarResultTitleHeight;
      for(const auto& line : row.matchLines) {
        // The match, marked with the same fill find-in-note uses, so a match is
        // a match wherever it is shown. Drawn under the text rather than over
        // it, and the line is already trimmed to keep the span in view.
        if(line.length > 0) {
          const std::string_view shown = line.text;
          const float from = static_cast<float>(text.width(shown.substr(0, line.start), snippetStyle));
          const float to = static_cast<float>(text.width(shown.substr(0, line.start + line.length), snippetStyle));
          ui::fillRounded(renderer, {row.rect.x + 16.0f + from, snippetY,
                                     std::max(2.0f, to - from), kSidebarSnippetHeight - 1.0f},
                          theme().findBg, 2.0f);
        }
        text.draw(line.text, row.rect.x + 16, snippetY, selected ? theme().accent : theme().dim, snippetStyle);
        snippetY += kSidebarSnippetHeight;
      }
      continue;
    }
    if(row.kind == SidebarRow::Kind::Tag) {
      const bool selected = selection.tag == row.tag;
      drawSelection(renderer, row.rect, selected, hot);
      text.draw("#" + ellipsizeToWidth(text, row.tag, static_cast<int>(row.rect.w - 40), rowStyle),
                list.x + 20, row.rect.y + 4, selected ? theme().accent : theme().dim, rowStyle);
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

    for(int depth = 0; depth < row.tree.depth; ++depth) {
      const float guideX = std::round(list.x + 17.0f + static_cast<float>(depth) * kSidebarIndent);
      fill(renderer, {guideX, row.rect.y, ui::kTreeGuideWidth, row.rect.h}, theme().hairline);
    }

    if(row.disclosure.w > 0.0f) {
      drawDisclosure(renderer, row.disclosure, row.tree.expanded,
                     ui.hovered(row.disclosure) ? theme().text : theme().dim);
    }
    const float labelX = indent + 18.0f;
    if(isNote) {
      drawNoteIcon(renderer, text, row.tree.icon, {indent, row.rect.y + 5.0f, 16.0f, 16.0f},
                   selected ? theme().accent : theme().dim);
    }
    const float countW = row.tree.noteCount > 0 && !isNote ? 26.0f : 8.0f;
    text.draw(ellipsizeToWidth(text, row.tree.label, static_cast<int>(row.rect.x + row.rect.w - labelX - countW), rowStyle),
              labelX, row.rect.y + 5.0f, selected || current ? theme().text : (isNote ? theme().muted : theme().text), rowStyle);
    if(!isNote && row.tree.noteCount > 0) {
      const auto count = std::to_string(row.tree.noteCount);
      text.draw(count, row.rect.x + row.rect.w - static_cast<float>(text.width(count, rowStyle)) - 10.0f,
                row.rect.y + 5.0f, current ? theme().accent : theme().dim, rowStyle);
    }
  }
  drawVerticalScrollbar(renderer, list, ui.sidebarScroll, ui.sidebarMaxScroll);
}

}
