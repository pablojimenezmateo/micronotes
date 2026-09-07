#include "app/Sidebar.h"

#include "app/Chrome.h"
#include "app/SidebarModel.h"
#include "core/editor/SingleLineView.h"
#include "ui/Metrics.h"
#include "ui/SearchScope.h"
#include "ui/TextUtil.h"
#include "ui/TagColors.h"
#include "ui/Theme.h"
#include "ui/TreeModel.h"

#include <algorithm>
#include <string>

namespace micronotes::app {
namespace {

using ui::ClipGuard;
using ui::Rect;
using ui::TextRenderer;
using ui::drawChevron;
using ui::drawEmptyMessage;
using ui::drawRow;
using ui::ellipsizeToWidth;
using ui::fill;
using ui::hLine;
using ui::stroke;
using ui::theme;

// The magnifier at the head of the search box. A drawn glyph rather than a
// word: the box used to be prefixed with the label "Find" *and* carry the
// placeholder "Search all notes", which read as "Find Search all notes" and
// said the same thing twice in two different registers. Named because its
// width sets where the field begins, so the mark and the geometry cannot drift.
constexpr float kSearchGlyphSize = 12.0f;

// Reserved at the trailing edge of a folder row for the note count, so a long
// folder name is ellipsized before it reaches the number rather than over it.
constexpr float kCountColumnWidth = 26.0f;

// Core measures text through a callback so it stays free of any font
// dependency; this binds it to the renderer actually drawing the field.
editor::TextWidthFn fieldMeasure(const TextRenderer& text) {
  const ui::TextStyle style = ui::chromeStyle();
  return [&text, style](std::string_view run) { return text.width(run, style); };
}

// The search field on top of the navigation it filters.
static void drawSidebarSearch(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect search = searchBoxRect(rect);
  const SearchBoxParts parts = searchBoxParts(rect, text);
  const bool focused = ui.focus == FocusArea::Search;
  const ui::TextStyle style = ui::chromeStyle();
  ui::drawTextFieldFrame(renderer, search, focused);
  ui.searchScopeToggle = parts.scope;
  ui.offerTooltip(ui.searchScopeToggle,
                  "Searching " + std::string(ui::searchScopeName(ui.searchScope)) + " - click to change");
  ui::drawSearchGlyph(renderer, parts.label, focused ? theme().accent : theme().textMuted);
  drawTextField(renderer, text, ui, ui.search, parts.field, focused, "Search all notes");
  ui::drawSurface(renderer, parts.scope, theme().surfaceRaised,
                  focused ? theme().accent : theme().border);
  const auto scopeLabel = ui::searchScopeLabel(ui.searchScope);
  text.draw(scopeLabel,
            std::round(parts.scope.x + (parts.scope.w - static_cast<float>(text.width(scopeLabel, style))) / 2.0f),
            ui::textTop(parts.scope, text, style), focused ? theme().accent : theme().textSecondary, style);
}

// The face a search snippet is set in. Read by the draw and by the trim that
// keeps a match inside the column, which are two places that have to agree
// about it or the trim is measured against the wrong font.
static ui::TextStyle snippetTextStyle() {
  return ui::chromeSmallStyle();
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
    drawEmptyMessage(text, "No notes with this tag",
                     "Nothing carries " + ui.state.selection().tag + " any more.",
                     x, y, width, "Esc  back to the tree");
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
  const ui::TextStyle style = ui::chromeStyle();
  const float line = static_cast<float>(text.lineHeight(style));
  const float toggle = std::min(box.h - ui::kSpace2, std::max(20.0f, line + ui::kSpace1));

  SearchBoxParts parts;
  parts.label = {box.x + ui::kSpace2, std::round(box.y + (box.h - kSearchGlyphSize) / 2.0f),
                 kSearchGlyphSize, kSearchGlyphSize};
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
  const ui::TextStyle style = ui::chromeStyle();
  const auto view = editor::layoutSingleLine(field.editor, box.w, field.scrollX, fieldMeasure(text));
  field.scrollX = view.scrollX;

  ClipGuard clip(renderer, box);
  const float originX = box.x - view.scrollX;
  if(focused && view.hasSelection) {
    fill(renderer, {originX + view.selectionStartX, box.y - 2.0f,
                    view.selectionEndX - view.selectionStartX, box.h + 4.0f}, theme().selectionFill);
  }
  if(field.empty() && !placeholder.empty()) {
    text.draw(placeholder, box.x, box.y, theme().textMuted, style);
  } else {
    text.draw(field.text(), originX, box.y, theme().textPrimary, style);
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
  fill(renderer, rect, theme().surfaceBackground);
  ClipGuard clip(renderer, rect);
  drawSidebarSearch(renderer, text, ui, rect);
  // A rule under the search band, so the field reads as chrome over the list
  // rather than as the first row of it.
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + ui::kSidebarSearchBand - 1.0f, theme().border);

  const Rect list = sidebarListRect(rect);
  ui.sidebarRect = list;
  const ui::TextStyle rowStyle = ui::chromeStyle();
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
      ui::drawSectionBand(renderer, text, row.rect, row.disclosure, row.label, row.trailing,
                          row.collapsed, hot);
      if(row.section) {
        ui.offerTooltip(row.rect, (row.collapsed ? "Show " : "Hide ") + row.label);
      }
      continue;
    }
    if(row.kind == SidebarRow::Kind::SearchResult) {
      const bool selected = row.noteId == selection.noteId;
      drawRow(renderer, row.rect, selected, hot);
      if(!selected && !hot) {
        hLine(renderer, row.rect.x + ui::kSpace2, row.rect.x + row.rect.w - ui::kSpace2,
              row.rect.y + row.rect.h, theme().border);
      }
      const Rect titleRow {row.rect.x, row.rect.y, row.rect.w, metrics.resultTitle};
      text.draw(ellipsizeToWidth(text, row.title, static_cast<int>(row.rect.w - kSidebarLabelX - ui::kSpace2), rowStyle),
                row.rect.x + kSidebarLabelX, ui::textTop(titleRow, text, rowStyle),
                selected ? theme().textPrimary : theme().textSecondary, rowStyle);
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
          ui::fill(renderer, {snippetX + from, snippetY,
                                     std::max(2.0f, to - from), metrics.snippet - 1.0f},
                          theme().searchMatch);
        }
        text.draw(line.text, snippetX, snippetY, selected ? theme().accent : theme().textMuted, snippetStyle);
        snippetY += metrics.snippet;
      }
      continue;
    }
    if(row.kind == SidebarRow::Kind::Tag) {
      const bool selected = selection.tag == row.tag;
      drawRow(renderer, row.rect, selected, hot);
      // The tag's own colour where a note row has its icon, so a tag row and
      // the dots on the notes carrying it are visibly the same mark. This is
      // the whole of what the `#` used to be doing -- saying "this is a tag" --
      // and a colour says it while also saying *which* tag.
      ui::drawTagDot(renderer,
                     {row.rect.x + kSidebarGutterX,
                      std::round(row.rect.y + (row.rect.h - kTagDotSize) / 2.0f),
                      kTagDotSize, kTagDotSize},
                     ui::tagColor(ui.state.workspace().tagColors, row.tag));
      // No `#`. It was a sigil in front of every row in a section already
      // headed TAGS, so it said nothing the position did not, and it cost the
      // first character of every name in the column.
      const int room = static_cast<int>(row.rect.w - kSidebarLabelX - ui::kSpace4);
      text.draw(ellipsizeToWidth(text, row.tag, room, rowStyle),
                row.rect.x + kSidebarLabelX, ui::textTop(row.rect, text, rowStyle),
                selected ? theme().textPrimary : theme().textSecondary, rowStyle);
      continue;
    }

    const bool isNote = row.tree.kind == ui::TreeRowKind::Note;
    // A folder is the current context and a note is the open document, so only
    // the note wears the accent strip: two markers at once would read as two
    // selections rather than as one place and one file.
    const bool selected = isNote && row.tree.noteId == selection.noteId;
    const bool current = !isNote && selection.tag.empty() && selection.folder == row.tree.folder;
    const bool dropTarget = ui.sidebarDropRow && *ui.sidebarDropRow == i;
    // Hover lifts the row's ground and nothing else, so the tree reads as "this
    // is what I would click" without masquerading as selected.
    drawRow(renderer, row.rect, theme().surfaceBackground,
            selected || current || hot || dropTarget, selected);
    if(dropTarget) stroke(renderer, row.rect, theme().accent);

    if(row.disclosure.w > 0.0f) {
      drawChevron(renderer, row.disclosure.x, row.disclosure.y + row.disclosure.h / 2.0f,
                  row.tree.expanded,
                  selected || current || ui.hovered(row.disclosure) ? theme().textPrimary
                                                                    : theme().textMuted);
    }
    if(isNote) {
      drawNoteIcon(renderer, text, row.tree.icon,
                   {gutterX, row.rect.y + (row.rect.h - kSidebarGutterWidth) / 2.0f,
                    kSidebarGutterWidth, kSidebarGutterWidth},
                   selected ? theme().accent : theme().textMuted);
    }
    // A dot per tag at the trailing edge, which is what joins a note's row to
    // the TAGS band below: the row named a folder and said nothing at all about
    // the tags on it, so the one way of organising a library that cuts across
    // the tree was invisible from the tree.
    //
    // The tags come off the note list rather than off the row. The row list is
    // O(library) and is built once per change; copying every note's tag names
    // into it would be a vector of strings per note to draw the three dozen
    // rows a panel can show. This is one hash lookup on a row about to be
    // painted.
    const library::NoteListItem* tagged =
      isNote ? ui.state.noteById(row.tree.noteId) : nullptr;
    const std::size_t tagCount = tagged ? tagged->tags.size() : 0;
    const float dotsW = tagCount > 0 ? kTagDotColumnWidth : 0.0f;

    const float labelY = ui::textTop(row.rect, text, rowStyle);
    const float countW = row.tree.noteCount > 0 && !isNote ? kCountColumnWidth
                       : dotsW > 0.0f                      ? dotsW
                                                           : ui::kSpace2;
    // A folder is a container and a note is a leaf, so the folder's name is the
    // brighter of the two -- the file tree's rule in every IDE, and the reverse
    // of what a list of documents would do.
    const SDL_Color ink = selected || current ? theme().textPrimary
                        : isNote              ? theme().textSecondary
                                              : theme().textPrimary;
    text.draw(ellipsizeToWidth(text, row.tree.label, static_cast<int>(row.rect.x + row.rect.w - labelX - countW), rowStyle),
              labelX, labelY, ink, rowStyle);
    if(!isNote && row.tree.noteCount > 0) {
      text.draw(std::to_string(row.tree.noteCount),
                row.rect.x + row.rect.w -
                  static_cast<float>(text.width(std::to_string(row.tree.noteCount), rowStyle)) - ui::kSpace2,
                labelY, current ? theme().accent : theme().textMuted, rowStyle);
    }
    if(tagCount > 0) {
      const auto& colors = ui.state.workspace().tagColors;
      const auto dots = tagDotRects(row.rect, tagCount);
      for(std::size_t d = 0; d < dots.size(); ++d) {
        // Past the cap the last dot stands for the tags that did not fit, so it
        // is drawn in the muted ink rather than in any one tag's colour -- a
        // marker, not a tag -- and its tooltip names them. Hiding them without
        // saying so would be worse than not drawing dots at all.
        const bool overflow = dots.size() < tagCount && d + 1 == dots.size();
        if(overflow) {
          ui::drawTagDot(renderer, dots[d], theme().textMuted);
          std::string rest;
          for(std::size_t t = d; t < tagged->tags.size(); ++t) {
            rest += (rest.empty() ? "" : ", ") + tagged->tags[t];
          }
          ui.offerTooltip(dots[d], std::to_string(tagged->tags.size() - d) + " more: " + rest);
          continue;
        }
        const auto& tag = tagged->tags[d];
        ui::drawTagDot(renderer, dots[d], ui::tagColor(colors, tag));
        // The dot is a control: it says which tags a note has, and clicking one
        // filters by it. So it names itself under the pointer -- a 7px disc
        // with no label is a colour until something tells you what it means.
        ui.offerTooltip(dots[d], "Filter by " + tag);
      }
    }
  }
  drawVerticalScrollbar(renderer, list, ui.sidebarScroll, ui.sidebarMaxScroll,
                        ui.scrollDragTarget == ScrollDragTarget::Sidebar);
}

}
