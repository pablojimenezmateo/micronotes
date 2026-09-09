#include "app/Sidebar.h"
#include "app/ContextMenus.h"

#include "app/Chrome.h"
#include "app/SidebarModel.h"
#include "core/editor/SingleLineView.h"
#include "ui/Metrics.h"
#include "library/SearchScope.h"

#include "ui/TagColors.h"
#include "ui/Theme.h"
#include "ui/TreeModel.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/Widgets.h"
#include "ui/ClipGuard.h"

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

// The search field on top of the navigation it filters.
static void drawSidebarSearch(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect search = searchBoxRect(rect);
  const SearchBoxParts parts = searchBoxParts(rect, text);
  const bool focused = ui.focus == FocusArea::Search;
  const ui::TextStyle style = ui::chromeStyle();
  ui::drawTextFieldFrame(renderer, search, focused);
  ui.sidebar.scopeToggle = parts.scope;
  ui.pointer.offerTooltip(ui.sidebar.scopeToggle,
                  "Searching " + std::string(library::searchScopeName(ui.fields.searchScope)) + " - click to change");
  ui::drawSearchGlyph(renderer, parts.label, focused ? theme().accent : theme().textMuted);
  drawTextField(renderer, text, ui, ui.fields.search, parts.field, focused, "Search all notes");
  ui::drawSurface(renderer, parts.scope, theme().surfaceRaised,
                  focused ? theme().accent : theme().border);
  const auto scopeLabel = library::searchScopeLabel(ui.fields.searchScope);
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
  if(!ui.state.catalog().isOpen()) {
    drawEmptyMessage(text, "No library", "Point micronotes at a folder of notes.",
                     x, y, width, ui::keysFor(ui::ActionId::Settings) + "  Settings");
  } else if(!ui.fields.search.empty()) {
    drawEmptyMessage(text, "Nothing matches", "No note contains \"" + ui.fields.search.text() + "\".",
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

namespace {

// What every row's paint needs and no row changes: the panel's list rect, the
// room the scrollbar wants at the trailing edge, the two faces, the row
// rhythm, what is selected, and how wide a snippet measures.
//
// One value rather than seven parameters because they are read-only for the
// whole frame and only mean anything together -- and because `trailingReserve`
// in particular has to be the same number for the counts, the tag dots and the
// labels' ellipsis, which is why `drawSidebar` asks for it once.
struct SidebarFrame {
  Rect list;
  float trailingReserve = 0.0f;
  ui::TextStyle rowStyle;
  ui::TextStyle snippetStyle;
  SidebarMetrics metrics;
  const ui::UiSelection& selection;
  std::function<float(std::string_view)> snippetWidth;
};

}

// One row of the sidebar: a band, a search result, a tag, or a line of the
// tree. Four shapes with four hit targets, dispatched on `row.kind`.
//
// Extracted from `drawSidebar`, which was 220 lines of which this was 177. It
// stays in this translation unit: it runs once per visible row per frame.
void drawSidebarRow(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui,
                    const SidebarRow& row, std::size_t index, const SidebarFrame& frame) {
  // Trimmed now that the row is known to be painted. See `fillSearchSnippets`.
  fillSearchSnippets(ui, index, frame.list.w, frame.snippetWidth);
  perf::addCounter(perf::CounterId::SidebarRowsDrawn);
  const bool hot = ui.pointer.over(row.rect);
  // Two columns, and every kind of row uses them: the gutter that holds a
  // disclosure or an icon, and the label beside it. Indent moves both.
  const float gutterX = row.rect.x + kSidebarGutterX + static_cast<float>(row.tree.depth) * kSidebarIndent;
  const float labelX = row.rect.x + kSidebarLabelX + static_cast<float>(row.tree.depth) * kSidebarIndent;

  if(row.kind == SidebarRow::Kind::SectionLabel) {
    ui::drawSectionBand(renderer, text, row.rect, row.disclosure, row.label, row.trailing,
                        row.collapsed, hot, frame.trailingReserve);
    // The NOTEBOOKS band is the drop target for the library root, which has
    // no row of its own in the tree. See `sidebarDropTargetAt`.
    if(ui.sidebar.drag.dropRow && *ui.sidebar.drag.dropRow == index) {
      stroke(renderer, row.rect, theme().accent);
    }
    if(row.section) {
      ui.pointer.offerTooltip(row.rect, (row.collapsed ? "Show " : "Hide ") + row.label);
    }
    return;
  }
  if(row.kind == SidebarRow::Kind::SearchResult) {
    const bool selected = row.noteId == frame.selection.noteId;
    drawRow(renderer, row.rect, selected, hot);
    if(!selected && !hot) {
      hLine(renderer, row.rect.x + ui::kSpace2, row.rect.x + row.rect.w - ui::kSpace2,
            row.rect.y + row.rect.h, theme().border);
    }
    const Rect titleRow {row.rect.x, row.rect.y, row.rect.w, frame.metrics.resultTitle};
    text.draw(ellipsizeToWidth(text, row.title, static_cast<int>(row.rect.w - kSidebarLabelX - ui::kSpace2), frame.rowStyle),
              row.rect.x + kSidebarLabelX, ui::textTop(titleRow, text, frame.rowStyle),
              selected ? theme().textPrimary : theme().textSecondary, frame.rowStyle);
    float snippetY = row.rect.y + frame.metrics.resultTitle;
    for(const auto& line : row.matchLines) {
      // The match, marked with the same fill find-in-note uses, so a match is
      // a match wherever it is shown. Drawn under the text rather than over
      // it, and the line is already trimmed to keep the span in view.
      const float snippetX = row.rect.x + kSidebarLabelX;
      if(line.length > 0) {
        const std::string_view shown = line.text;
        const float from = static_cast<float>(text.width(shown.substr(0, line.start), frame.snippetStyle));
        const float to = static_cast<float>(text.width(shown.substr(0, line.start + line.length), frame.snippetStyle));
        ui::fill(renderer, {snippetX + from, snippetY,
                                   std::max(2.0f, to - from), frame.metrics.snippet - 1.0f},
                        theme().searchMatch);
      }
      text.draw(line.text, snippetX, snippetY, selected ? theme().accent : theme().textMuted, frame.snippetStyle);
      snippetY += frame.metrics.snippet;
    }
    return;
  }
  if(row.kind == SidebarRow::Kind::Tag) {
    const bool selected = frame.selection.tag == row.tag;
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
    text.draw(ellipsizeToWidth(text, row.tag, room, frame.rowStyle),
              row.rect.x + kSidebarLabelX, ui::textTop(row.rect, text, frame.rowStyle),
              selected ? theme().textPrimary : theme().textSecondary, frame.rowStyle);
    return;
  }

  const bool isNote = row.tree.kind == ui::TreeRowKind::Note;
  const bool isFile = row.tree.kind == ui::TreeRowKind::File;
  // A companion row -- a file, or a folder in a files area -- is neither the
  // open document nor the current notebook, so it never wears either mark.
  const bool companion = isFile || row.tree.kind == ui::TreeRowKind::FilesFolder;
  // A folder is the current context and a note is the open document, so only
  // the note wears the accent strip: two markers at once would read as two
  // selections rather than as one place and one file.
  const bool selected = isNote && row.tree.noteId == frame.selection.noteId;
  const bool current = !isNote && !companion && frame.selection.tag.empty() && frame.selection.folder == row.tree.folder;
  const bool dropTarget = ui.sidebar.drag.dropRow && *ui.sidebar.drag.dropRow == index;
  // Hover lifts the row's ground and nothing else, so the tree reads as "this
  // is what I would click" without masquerading as selected.
  drawRow(renderer, row.rect, theme().surfaceBackground,
          selected || current || hot || dropTarget, selected);
  if(dropTarget) stroke(renderer, row.rect, theme().accent);

  if(row.disclosure.w > 0.0f) {
    drawChevron(renderer, row.disclosure.x, row.disclosure.y + row.disclosure.h / 2.0f,
                row.tree.expanded,
                selected || current || ui.pointer.over(row.disclosure) ? theme().textPrimary
                                                                  : theme().textMuted);
  }
  if(isNote) {
    drawNoteIcon(renderer, row.tree.icon,
                 {gutterX, row.rect.y + (row.rect.h - kSidebarGutterWidth) / 2.0f,
                  kSidebarGutterWidth, kSidebarGutterWidth},
                 selected ? theme().accent : theme().textMuted);
  } else if(isFile) {
    // One mark for every kind of file; see `ui::drawFileGlyph`.
    ui::drawFileGlyph(renderer,
                      {gutterX, row.rect.y + (row.rect.h - kSidebarGutterWidth) / 2.0f,
                       kSidebarGutterWidth, kSidebarGutterWidth},
                      theme().textMuted);
  }
  // A dot per tag at the trailing edge, which is what joins a note's row to
  // the TAGS band below: the row named a folder and said nothing at all about
  // the tags on it, so the one way of organising a library that cuts across
  // the tree was invisible from the tree.
  //
  // The tags come off the note frame.list rather than off the row. The row frame.list is
  // O(library) and is built once per change; copying every note's tag names
  // into it would be a vector of strings per note to draw the three dozen
  // rows a panel can show. This is one hash lookup on a row about to be
  // painted.
  const library::NoteListItem* tagged =
    isNote ? ui.state.catalog().noteById(row.tree.noteId) : nullptr;
  const std::size_t tagCount = tagged ? tagged->tags.size() : 0;
  const float dotsW = tagCount > 0 ? kTagDotColumnWidth : 0.0f;

  const float labelY = ui::textTop(row.rect, text, frame.rowStyle);
  const float countW = frame.trailingReserve
                     + (row.tree.noteCount > 0 && !isNote ? kCountColumnWidth
                      : dotsW > 0.0f                      ? dotsW
                                                          : ui::kSpace2);
  // A folder is a container and a note is a leaf, so the folder's name is the
  // brighter of the two -- the file tree's rule in every IDE, and the reverse
  // of what a frame.list of documents would do.
  //
  // A files directory takes the leaf's ink even though it wears a chevron: it
  // is a container of files rather than a notebook, and the dimmer name is
  // what says so at a glance.
  const SDL_Color ink = selected || current ? theme().textPrimary
                      : isNote || companion ? theme().textSecondary
                                            : theme().textPrimary;
  text.draw(ellipsizeToWidth(text, row.tree.label, static_cast<int>(row.rect.x + row.rect.w - labelX - countW), frame.rowStyle),
            labelX, labelY, ink, frame.rowStyle);
  if(!isNote && row.tree.noteCount > 0) {
    text.draw(std::to_string(row.tree.noteCount),
              row.rect.x + row.rect.w - frame.trailingReserve -
                static_cast<float>(text.width(std::to_string(row.tree.noteCount), frame.rowStyle)) - ui::kSpace2,
              labelY, current ? theme().accent : theme().textMuted, frame.rowStyle);
  }
  if(tagCount > 0) {
    const auto& colors = ui.state.workspace().tagColors;
    const auto dots = tagDotRects(row.rect, tagCount, frame.trailingReserve);
    for(std::size_t d = 0; d < dots.size(); ++d) {
      // Past the cap the last dot stands for the tags that did not fit, so it
      // is drawn in the muted ink rather than in any one tag's colour -- a
      // marker, not a tag -- and its tooltip names them. Hiding them without
      // saying so would be worse than not drawing dots at all.
      const bool overflow = dots.size() < tagCount && d + 1 == dots.size();
      if(overflow) {
        ui::drawTagDot(renderer, dots[d], theme().textMuted);
        // The names it stands for, and only those. A count and a prefix would
        // be the tooltip explaining itself instead of answering the one
        // question a coloured dot raises.
        std::string rest;
        for(std::size_t t = d; t < tagged->tags.size(); ++t) {
          rest += (rest.empty() ? "" : ", ") + tagged->tags[t];
        }
        ui.pointer.offerTooltip(dots[d], rest);
        continue;
      }
      const auto& tag = tagged->tags[d];
      ui::drawTagDot(renderer, dots[d], ui::tagColor(colors, tag));
      // The tag's name, and nothing else. A 7px disc raises exactly one
      // question -- *which* tag -- and "Filter by work" answers it while also
      // narrating a click the reader has not made yet.
      ui.pointer.offerTooltip(dots[d], tag);
    }
  }
}

void drawSidebar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().surfaceBackground);
  ClipGuard clip(renderer, rect);
  drawSidebarSearch(renderer, text, ui, rect);
  // A rule under the search band, so the field reads as chrome over the list
  // rather than as the first row of it.
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + ui::kSidebarSearchBand - 1.0f, theme().border);

  const Rect list = sidebarListRect(rect);
  // What the trailing edge of every row and band has to keep clear, asked once
  // for the frame so the counts, the dots and the labels' ellipsis all agree.
  // Zero when the list fits, which is the whole reason it is not a constant.
  const float trailingReserve = ui::scrollbarReserve(list, ui.sidebar.list.scroll(),
                                                     ui.sidebar.list.maxScroll());
  // Recorded for the hit tests, which must place a tag dot exactly where this
  // frame painted it. See `SidebarState::trailingReserve`.
  ui.sidebar.trailingReserve = trailingReserve;
  const ui::TextStyle rowStyle = ui::chromeStyle();
  const ui::TextStyle snippetStyle = snippetTextStyle();
  // Measured here and handed to the model, which stays free of the font: the
  // rows have to be as tall as the text they hold, and only the renderer knows
  // how tall that is.
  const SidebarMetrics metrics = sidebarMetrics(text.lineHeight(rowStyle), text.lineHeight(snippetStyle));
  buildSidebarRows(ui, list, metrics);
  if(ui.sidebar.rows.empty()) {
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
  const auto [firstRow, lastRow] = sidebarRowRange(ui.sidebar.rows, list.y, list.y + list.h);
  const SidebarFrame frame {list, trailingReserve, rowStyle, snippetStyle, metrics, selection,
                           snippetWidth};
  for(std::size_t i = firstRow; i < lastRow; ++i) {
    drawSidebarRow(renderer, text, ui, ui.sidebar.rows[i], i, frame);
  }
  drawVerticalScrollbar(renderer, list, ui.sidebar.list.scroll(), ui.sidebar.list.maxScroll(),
                        ui.pointer.scrollDrag == ScrollDrag::Sidebar);
}

bool pressSidebar(TextRenderer& text, UiRuntime& ui, Rect sidebar, float x, float y,
                  Uint8 button) {
  if(button == SDL_BUTTON_LEFT) {
    if(std::abs(x - (sidebar.x + sidebar.w)) <= 4.0f) {
      ui.sidebar.resizing = true;
      return true;
    }
  }

  if(contains(sidebar, x, y)) {
    // The scrollbar first: its thumb overlaps the trailing edge of every row it
    // covers, and a press on a handle has to move the handle rather than select
    // whatever it happens to be lying on.
    if(button == SDL_BUTTON_LEFT) {
      const Rect list = sidebarListRect(sidebar);
      const auto bar = ui::scrollbarGeometry(list, ui.sidebar.list.scroll(), ui.sidebar.list.maxScroll());
      if(bar && contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
        ui.pointer.scrollDrag = ScrollDrag::Sidebar;
        ui.pointer.scrollDragOffsetY = y - bar->thumb.y;
        return true;
      }
    }
    // The search field is part of the sidebar but not part of its row list, so
    // it takes the click before any row arithmetic happens.
    if(contains(searchBoxRect(sidebar), x, y)) {
      if(contains(ui.sidebar.scopeToggle, x, y)) {
        ui.fields.searchScope = library::nextSearchScope(ui.fields.searchScope);
        ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
        ui.status = "Searching " + std::string(library::searchScopeName(ui.fields.searchScope));
        return true;
      }
      // Clicking a text field puts the caret where you clicked. Before, it only
      // moved focus, and the insertion point stayed pinned to the end.
      const Rect fieldRect = searchTextRect(sidebar, text);
      ui.focus = FocusArea::Search;
      const auto offset = fieldOffsetAtX(text, ui.fields.search, fieldRect, x);
      ui.fields.search.editor.moveCursor(offset);
      ui.fieldSelect.active = true;
      ui.fieldSelect.anchor = offset;
      return true;
    }

    ui.focus = FocusArea::Folders;
    const auto index = sidebarRowAt(ui, x, y);
    if(!index) {
      if(button == SDL_BUTTON_RIGHT) openFolderMenu(ui, x, y);
      return true;
    }
    const SidebarRow row = ui.sidebar.rows[*index];
    ui.sidebar.cursor = static_cast<int>(*index);
    // What a press on a row means lives with the rows. See `pressSidebarRow`.
    pressSidebarRow(ui, row, x, y, button);
    return true;
  }
  return false;
}

}
