#include "app/SidebarModel.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/TreeModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace micronotes::app {
using ui::Rect;
using ui::contains;

// The lines a result will show, which is every non-empty matching line it
// carries. Counted here and trimmed in `fillSearchSnippets`, and the two have to
// agree or a row's height will not match its text.
std::size_t countMatchLines(const library::SearchResult& result) {
  if(result.snippets.empty()) return result.matchLine.empty() ? 0 : 1;
  std::size_t lines = 0;
  for(const auto& snippet : result.snippets) {
    if(!snippet.matchLine.empty()) ++lines;
  }
  return lines;
}

float searchResultRowHeight(std::size_t matchLines) {
  return kSidebarResultTitleHeight + static_cast<float>(matchLines) * kSidebarSnippetHeight + 4.0f;
}

// The results the sidebar is listing, recomputed only when the question or the
// library has changed. buildSidebarRows() runs on every frame, and each query
// is a hit on SQLite.
const std::vector<library::SearchResult>& searchResults(UiRuntime& ui) {
  const auto& selection = ui.state.selection();
  if(!ui.searchCacheValid || ui.searchCacheQuery != selection.search ||
     ui.searchCacheScope != selection.searchScope || ui.searchCacheRevision != ui.state.revision()) {
    ui.searchCacheQuery = selection.search;
    ui.searchCacheScope = selection.searchScope;
    ui.searchCacheRevision = ui.state.revision();
    ui.searchCache = ui.state.currentSearchResults();
    ui.searchCacheValid = true;
  }
  return ui.searchCache;
}


namespace {

// The row list, from the library. O(library) rather than O(viewport): the tree
// relativises a path and builds a map key for every note before it can place
// the first row, and every row is a string, a path and a rect. Called through
// buildSidebarRows(), which is what keeps it off the frame path.
void rebuildSidebarRows(UiRuntime& ui, Rect rect) {
  ui.sidebarRows.clear();
  const float top = rect.y + 12.0f;
  float y = top - static_cast<float>(ui.sidebarScroll);

  const auto pushLabel = [&](std::string label) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarLabelHeight};
    ui.sidebarRows.push_back(std::move(row));
    y += kSidebarLabelHeight;
  };
  const auto pushTreeRow = [&](ui::TreeRow tree) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tree;
    row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarRowHeight};
    if(tree.expandable) {
      row.disclosure = {rect.x + 10.0f + static_cast<float>(tree.depth) * kSidebarIndent, y + 5.0f, 16.0f, 16.0f};
    }
    row.tree = std::move(tree);
    ui.sidebarRows.push_back(std::move(row));
    y += kSidebarRowHeight;
  };

  const auto& notes = ui.state.allNotes();
  const auto root = ui.state.libraryRoot();
  // A shortcut list is a flat list of notes, drawn with the same row the tree
  // uses so a note looks and behaves the same wherever it is listed.
  const auto pushNoteShortcuts = [&](const std::vector<std::string>& ids, std::size_t limit) {
    std::size_t drawn = 0;
    for(const auto& id : ids) {
      if(drawn >= limit) break;
      // By id index rather than by scan. Thirteen shortcuts against a thousand
      // notes was thirteen thousand string compares to draw thirteen rows.
      const auto* found = ui.state.noteById(id);
      if(!found) continue;
      ui::TreeRow tree;
      tree.kind = ui::TreeRowKind::Note;
      tree.depth = 0;
      tree.folder = found->folder;
      tree.noteId = found->id;
      tree.label = found->title;
      tree.icon = found->icon;
      pushTreeRow(std::move(tree));
      ++drawn;
    }
    return drawn;
  };

  const auto pushFlatNote = [&](const library::NoteListItem& note) {
    ui::TreeRow tree;
    tree.kind = ui::TreeRowKind::Note;
    tree.depth = 0;
    tree.folder = note.folder;
    tree.noteId = note.id;
    tree.label = note.title;
    tree.icon = note.icon;
    pushTreeRow(std::move(tree));
  };
  // Whatever the list ended up holding, it scrolls the same way.
  const auto finish = [&]() {
    perf::addCounter(perf::CounterId::SidebarRowsBuilt, ui.sidebarRows.size());
    const float contentHeight = y + static_cast<float>(ui.sidebarScroll) - top;
    ui.sidebarRowsKey.contentHeight = contentHeight;
    ui.sidebarMaxScroll = std::max(0, static_cast<int>(std::ceil(contentHeight - (rect.h - 24.0f))));
    ui.sidebarScroll = std::clamp(ui.sidebarScroll, 0, ui.sidebarMaxScroll);
  };

  // A running query replaces the tree rather than appearing beside it. The
  // sidebar answers one question at a time, and Esc puts the tree back.
  if(!ui.search.empty()) {
    const auto& results = searchResults(ui);
    // Nothing at all rather than a heading over a hole: a bare "0 RESULTS" is
    // a row saying the list is empty, drawn instead of the empty state that
    // says so and also says how to get out of it.
    if(results.empty()) {
      finish();
      return;
    }
    pushLabel(std::to_string(results.size()) + (results.size() == 1 ? " RESULT" : " RESULTS"));
    for(std::size_t i = 0; i < results.size(); ++i) {
      const auto& result = results[i];
      SidebarRow row;
      row.kind = SidebarRow::Kind::SearchResult;
      row.noteId = result.id;
      row.title = result.title;
      row.resultIndex = i;
      // How many lines the row will show, counted rather than measured. Every
      // line `fillSearchSnippets` will trim is a non-empty one, so the two
      // agree without either of them doing the other's work.
      row.matchLineCount = countMatchLines(result);
      row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, searchResultRowHeight(row.matchLineCount)};
      y += row.rect.h;
      ui.sidebarRows.push_back(std::move(row));
    }
    finish();
    return;
  }

  // A tag is a filter over the library, so choosing one lists what carries it
  // instead of the tree it cuts across.
  if(!ui.state.selection().tag.empty()) {
    pushLabel("#" + ui.state.selection().tag);
    for(const auto& note : notes) {
      if(std::find(note.tags.begin(), note.tags.end(), ui.state.selection().tag) == note.tags.end()) continue;
      pushFlatNote(note);
    }
    finish();
    return;
  }

  if(!ui.state.workspace().favorites.empty()) {
    const std::size_t before = ui.sidebarRows.size();
    pushLabel("FAVORITES");
    if(pushNoteShortcuts(ui.state.workspace().favorites, 8) == 0) {
      // Every favourite has been deleted since; the heading would be a lie.
      ui.sidebarRows.resize(before);
      y -= kSidebarLabelHeight;
    }
  }

  for(auto& row : ui.tree.rows(ui.state.folders(), notes, root)) pushTreeRow(std::move(row));

  const auto& tags = ui.state.tags();
  if(!tags.empty()) {
    // Tags are a filter over the tree, not a second way to organise it, so they
    // sit below it and read quieter.
    pushLabel("TAGS");
    for(const auto& tag : tags) {
      SidebarRow row;
      row.kind = SidebarRow::Kind::Tag;
      row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarTagHeight};
      row.tag = tag;
      ui.sidebarRows.push_back(std::move(row));
      y += kSidebarTagHeight;
    }
  }

  if(!ui.state.workspace().recents.empty()) {
    const std::size_t before = ui.sidebarRows.size();
    pushLabel("RECENT");
    if(pushNoteShortcuts(ui.state.workspace().recents, 5) == 0) {
      ui.sidebarRows.resize(before);
      y -= kSidebarLabelHeight;
    }
  }

  finish();
  // A list of nothing but headings is a heading over a hole. The empty message
  // says what happened instead, and hit-testing must not find rows that are not
  // drawn -- so the emptiness is decided here, with the build, rather than by
  // the draw clearing a list the memo would then hand back next frame.
  const bool onlyLabels = std::none_of(ui.sidebarRows.begin(), ui.sidebarRows.end(),
                                       [](const auto& row) { return row.kind != SidebarRow::Kind::SectionLabel; });
  // The library's own root, alone, is the same hole with a disclosure triangle
  // on it: it opens onto nothing and selects the folder you are already in. A
  // fresh library showed exactly that one row, so the "no notes yet" message --
  // which is also where the shortcut for writing the first one is -- was
  // written and then never reachable.
  const bool rootAlone = ui.sidebarRows.size() == 1 &&
    ui.sidebarRows.front().kind == SidebarRow::Kind::Tree &&
    ui.sidebarRows.front().tree.kind == ui::TreeRowKind::Folder &&
    ui.sidebarRows.front().tree.folder.empty();
  if(onlyLabels || rootAlone) ui.sidebarRows.clear();
}

}

// The memo in front of the rebuild above.
//
// The row list is a pure function of the library revision, what the tree has
// open, the query, the tag filter, the shortcut lists and the panel's size.
// None of those change on a typical frame; the panel's origin and the scroll
// do, and both are an offset over a list already built. Rebuilding regardless
// was ~0.7 ms of a ~1.0 ms frame on a 400-note library, which is most of the
// frame spent re-deriving three dozen visible rows from four hundred notes.
void buildSidebarRows(UiRuntime& ui, Rect rect) {
  const auto& workspace = ui.state.workspace();
  const auto& previous = ui.sidebarRowsKey;
  const bool reusable = previous.valid &&
    previous.stateRevision == ui.state.revision() &&
    previous.treeRevision == ui.tree.revision() &&
    previous.search == ui.search.text() &&
    previous.searchScope == ui.searchScope &&
    previous.tag == ui.state.selection().tag &&
    previous.favorites == workspace.favorites &&
    previous.recents == workspace.recents &&
    previous.width == rect.w &&
    previous.height == rect.h;

  if(reusable) {
    // Clamp first: the scroll the rows are shifted by has to be the one they
    // will be drawn at, or a clamp after the shift leaves them a scroll behind.
    ui.sidebarMaxScroll = std::max(0, static_cast<int>(std::ceil(previous.contentHeight - (rect.h - 24.0f))));
    ui.sidebarScroll = std::clamp(ui.sidebarScroll, 0, ui.sidebarMaxScroll);
    const float dx = rect.x - previous.originX;
    const float dy = (rect.y - previous.originY) - static_cast<float>(ui.sidebarScroll - previous.scroll);
    if(dx != 0.0f || dy != 0.0f) {
      for(auto& row : ui.sidebarRows) {
        row.rect.x += dx;
        row.rect.y += dy;
        row.disclosure.x += dx;
        row.disclosure.y += dy;
      }
    }
    ui.sidebarRowsKey.originX = rect.x;
    ui.sidebarRowsKey.originY = rect.y;
    ui.sidebarRowsKey.scroll = ui.sidebarScroll;
    perf::addCounter(perf::CounterId::SidebarRowsReused, ui.sidebarRows.size());
    return;
  }

  rebuildSidebarRows(ui, rect);

  auto& key = ui.sidebarRowsKey;
  key.valid = true;
  key.stateRevision = ui.state.revision();
  key.treeRevision = ui.tree.revision();
  key.search = ui.search.text();
  key.searchScope = ui.searchScope;
  key.tag = ui.state.selection().tag;
  key.favorites = workspace.favorites;
  key.recents = workspace.recents;
  key.width = rect.w;
  key.height = rect.h;
  key.originX = rect.x;
  key.originY = rect.y;
  // Read back rather than remembered: finish() clamps it.
  key.scroll = ui.sidebarScroll;
}

void fillSearchSnippets(UiRuntime& ui, std::size_t index, float width,
                        const SnippetMeasure& measure) {
  if(index >= ui.sidebarRows.size()) return;
  SidebarRow& row = ui.sidebarRows[index];
  if(row.kind != SidebarRow::Kind::SearchResult || row.matchLinesBuilt) return;
  row.matchLinesBuilt = true;
  const auto& results = searchResults(ui);
  // The row list and the result list share a key, so an index into one holds in
  // the other -- but the row list survives a scroll without being rebuilt, so
  // the note id is checked rather than assumed. A row that has come adrift draws
  // its title with no lines under it, which is what a title-only match looks
  // like anyway.
  if(row.resultIndex >= results.size()) return;
  const auto& result = results[row.resultIndex];
  if(result.id != row.noteId) return;

  // What the snippets are trimmed to: the row's width, less the indent they are
  // drawn at and the same margin on the other side.
  const int room = static_cast<int>(width - 16.0f - 28.0f);
  const auto push = [&](const std::string& line, std::size_t at, std::size_t length) {
    if(line.empty()) return;
    perf::addCounter(perf::CounterId::SidebarSnippetsTrimmed);
    row.matchLines.push_back(ui::snippetAroundMatch(line, at, length, room, measure));
  };
  if(result.snippets.empty()) {
    push(result.matchLine, result.matchStart, result.matchLength);
  } else {
    for(const auto& snippet : result.snippets) {
      push(snippet.matchLine, snippet.matchStart, snippet.matchLength);
    }
  }
}

// The row under the pointer, or nothing when the pointer is off the list.
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, Rect sidebar, float x, float y) {
  if(!contains(sidebar, x, y)) return std::nullopt;
  for(std::size_t i = 0; i < ui.sidebarRows.size(); ++i) {
    if(ui.sidebarRows[i].kind == SidebarRow::Kind::SectionLabel) continue;
    if(contains(ui.sidebarRows[i].rect, x, y)) return i;
  }
  return std::nullopt;
}

}
