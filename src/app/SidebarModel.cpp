#include "app/SidebarModel.h"

#include "app/ContextMenus.h"
#include "app/Notes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/RowBand.h"
#include "ui/RowCursor.h"
#include "ui/TreeModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

SidebarMetrics sidebarMetrics(int uiLineHeight, int snippetLineHeight) {
  const float line = static_cast<float>(uiLineHeight);
  const float small = static_cast<float>(snippetLineHeight);
  SidebarMetrics metrics;
  // A row is its line of text plus a hair of air, not plus a whole spacing
  // step: the tree is meant to be dense, and the step was what made a
  // twenty-note folder need scrolling.
  metrics.row = std::max(metrics.row, line + 2.0f);
  metrics.tag = std::max(metrics.tag, line + 2.0f);
  // A heading over a list wants the air above it that separates it from the
  // list before, which is why it is the one row with a whole step of padding.
  metrics.label = std::max(metrics.label, line + ui::kSpace3);
  metrics.resultTitle = std::max(metrics.resultTitle, line + 2.0f);
  metrics.snippet = std::max(metrics.snippet, small + 1.0f);
  return metrics;
}

float searchResultRowHeight(std::size_t matchLines, const SidebarMetrics& metrics) {
  return metrics.resultTitle + static_cast<float>(matchLines) * metrics.snippet + ui::kSpace1;
}

// The results the sidebar is listing, recomputed only when the question or the
// library has changed. buildSidebarRows() runs on every frame, and each query
// is a hit on SQLite.
const std::vector<library::SearchResult>& searchResults(UiRuntime& ui) {
  const auto& selection = ui.state.selection();
  const SearchKey key {selection.search, selection.searchScope, ui.state.revision()};
  if(const auto* results = ui.sidebar.searchResults.get(key)) return *results;
  return ui.sidebar.searchResults.store(key, ui.state.currentSearchResults());
}

namespace {

// The sidebar's row list, built one band at a time.
//
// A type rather than a function with seven lambdas in it. The lambdas shared a
// running `y`, the row vector and the metrics, which is why the function could
// not be split by moving pieces out of it -- it was already a builder, with its
// state in the enclosing scope and its invariant maintained by seven closures
// agreeing to advance by exactly the height they had just used.
//
// That invariant is load-bearing: `sidebarRowRange` binary-searches the list on
// every frame and every mouse-motion event, and it is only correct because the
// rows tile. `ui::RowCursor` owns it now, so a band added here cannot break the
// band query over there.
class RowBuilder {
public:
  RowBuilder(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics)
    : ui_(ui), rect_(rect), metrics_(metrics),
      cursor_(rect.x, rect.w, rect.y + kSidebarListPadding -
                                static_cast<float>(ui.sidebar.list.scroll())) {
    ui_.sidebar.rows.clear();
  }

  // A caption on what the list is showing: the result count, the tag being
  // filtered by. It heads the list the same way a band does and reads the same,
  // but there is nothing under it to shut, so it gets no chevron.
  //
  // Title case throughout, not caps. microide's sections are "Staged" and
  // "Unstaged", and at 13px the reason is legible: uppercase in a mono face is
  // a row of same-width rectangles with no ascenders or descenders to tell them
  // apart, so a caps heading reads as a texture before it reads as a word.
  void caption(std::string label) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    // Full panel width, unlike the rows it heads. A band that stops short of
    // the panel edges reads as a card sitting in the list rather than as a
    // division of it, and a division is the whole point.
    row.rect = cursor_.place(metrics_.label);
    ui_.sidebar.rows.push_back(std::move(row));
  }

  // A band that can be shut. Returns whether to go on and emit its contents,
  // so a collapsed section is one `if` at the call site rather than a branch
  // around each of the four groups.
  bool section(ui::SidebarSection which, std::string label, std::size_t count) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    row.section = which;
    row.collapsed = ui_.state.workspace().sectionCollapsed(which);
    // The count is worth most on a band that is shut, which is the one case
    // where what is under it cannot be counted by looking.
    if(count > 0) row.trailing = std::to_string(count);
    row.rect = cursor_.place(metrics_.label);
    // The same control a folder row wears, at the panel's own inset rather
    // than on the label column the rows use. That outdent is the visual
    // difference between a heading and a row -- see `ui::kSectionLabelX`.
    row.disclosure = {rect_.x + ui::kSectionChevronX,
                      row.rect.y + (metrics_.label - kSidebarGutterWidth) / 2.0f,
                      kSidebarGutterWidth, kSidebarGutterWidth};
    const bool collapsed = row.collapsed;
    ui_.sidebar.rows.push_back(std::move(row));
    return !collapsed;
  }

  void treeRow(ui::TreeRow tree) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tree;
    row.rect = cursor_.place(metrics_.row, ui::kSpace2);
    if(tree.expandable) {
      row.disclosure = {row.rect.x + kSidebarGutterX + static_cast<float>(tree.depth) * kSidebarIndent,
                        row.rect.y + (metrics_.row - kSidebarGutterWidth) / 2.0f,
                        kSidebarGutterWidth, kSidebarGutterWidth};
    }
    row.tree = std::move(tree);
    ui_.sidebar.rows.push_back(std::move(row));
  }

  // A note listed outside the tree -- in a shortcut band, or under a tag
  // filter. Drawn with the row the tree uses so a note looks and behaves the
  // same wherever it is listed.
  void noteRow(const library::NoteListItem& note) {
    ui::TreeRow tree;
    tree.kind = ui::TreeRowKind::Note;
    tree.depth = 0;
    tree.folder = note.folder;
    tree.noteId = note.id;
    tree.label = note.title;
    tree.icon = note.icon;
    treeRow(std::move(tree));
  }

  void noteShortcuts(const std::vector<std::string>& ids, std::size_t limit) {
    std::size_t drawn = 0;
    for(const auto& id : ids) {
      if(drawn >= limit) break;
      // By id index rather than by scan. Thirteen shortcuts against a thousand
      // notes was thirteen thousand string compares to draw thirteen rows.
      const auto* found = ui_.state.noteById(id);
      if(!found) continue;
      noteRow(*found);
      ++drawn;
    }
  }

  void tagRow(const std::string& tag) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tag;
    row.rect = cursor_.place(metrics_.tag, ui::kSpace2);
    row.tag = tag;
    ui_.sidebar.rows.push_back(std::move(row));
  }

  void searchResult(const library::SearchResult& result, std::size_t index) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SearchResult;
    row.noteId = result.id;
    row.title = result.title;
    row.resultIndex = index;
    // How many lines the row will show, counted rather than measured. Every
    // line `fillSearchSnippets` will trim is a non-empty one, so the two
    // agree without either of them doing the other's work.
    row.matchLineCount = countMatchLines(result);
    row.rect = cursor_.place(searchResultRowHeight(row.matchLineCount, metrics_), ui::kSpace2);
    ui_.sidebar.rows.push_back(std::move(row));
  }

  // How many of a shortcut list still name a note. Counted before the band is
  // pushed rather than after, because a band that can be shut has to carry its
  // count whether or not its rows were emitted -- and because a heading over
  // nothing at all is a lie whichever way round it is built. This is the
  // `resize` the two shortcut lists used to do to take a heading back.
  std::size_t resolvable(const std::vector<std::string>& ids, std::size_t limit) const {
    std::size_t found = 0;
    for(const auto& id : ids) {
      if(found >= limit) break;
      if(ui_.state.noteById(id)) ++found;
    }
    return found;
  }

  // Whatever the list ended up holding, it scrolls the same way.
  void finish() {
    perf::addCounter(perf::CounterId::SidebarRowsBuilt, ui_.sidebar.rows.size());
    const float contentHeight = cursor_.height();
    ui_.sidebar.rowsKey.contentHeight = contentHeight;
    ui_.sidebar.list.setContent(rect_.h - kSidebarListPadding * 2.0f, contentHeight);
  }

private:
  UiRuntime& ui_;
  Rect rect_;
  const SidebarMetrics& metrics_;
  ui::RowCursor cursor_;
};

// The row list, from the library. O(library) rather than O(viewport): the tree
// relativises a path and builds a map key for every note before it can place
// the first row, and every row is a string, a path and a rect. Called through
// buildSidebarRows(), which is what keeps it off the frame path.
void rebuildSidebarRows(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
  RowBuilder build(ui, rect, metrics);

  const auto& notes = ui.state.allNotes();

  // A running query replaces the tree rather than appearing beside it. The
  // sidebar answers one question at a time, and Esc puts the tree back.
  if(!ui.fields.search.empty()) {
    const auto& results = searchResults(ui);
    // Nothing at all rather than a heading over a hole: a bare "0 RESULTS" is
    // a row saying the list is empty, drawn instead of the empty state that
    // says so and also says how to get out of it.
    if(results.empty()) {
      build.finish();
      return;
    }
    build.caption(std::to_string(results.size()) + (results.size() == 1 ? " result" : " results"));
    for(std::size_t i = 0; i < results.size(); ++i) build.searchResult(results[i], i);
    build.finish();
    return;
  }

  // A tag is a filter over the library, so choosing one lists what carries it
  // instead of the tree it cuts across. No `#`: the sidebar draws a tag's own
  // colour beside its name everywhere else, and a sigil as well is the same
  // thing said twice in two registers.
  if(!ui.state.selection().tag.empty()) {
    build.caption(ui.state.selection().tag);
    for(const auto& note : notes) {
      if(std::find(note.tags.begin(), note.tags.end(), ui.state.selection().tag) == note.tags.end()) continue;
      build.noteRow(note);
    }
    build.finish();
    return;
  }

  const auto& favorites = ui.state.workspace().favorites;
  if(const std::size_t count = build.resolvable(favorites, kMaxFavoriteRows); count > 0) {
    if(build.section(ui::SidebarSection::Favorites, "Favorites", count)) {
      build.noteShortcuts(favorites, kMaxFavoriteRows);
    }
  }

  // The tree is a band like the other three. It had no heading at all, which is
  // most of what made the divisions unclear: an unlabelled group between two
  // labelled ones reads as the tail of the one above it.
  auto treeRows = ui.sidebar.tree.rows(ui.state.folders(), notes);
  if(!treeRows.empty()) {
    if(build.section(ui::SidebarSection::Notebooks, "Notebooks", notes.size())) {
      for(auto& row : treeRows) build.treeRow(std::move(row));
    }
  }

  const auto& tags = ui.state.tags();
  if(!tags.empty()) {
    // Tags are a filter over the tree, not a second way to organise it, so they
    // sit below it.
    if(build.section(ui::SidebarSection::Tags, "Tags", tags.size())) {
      for(const auto& tag : tags) build.tagRow(tag);
    }
  }

  const auto& recents = ui.state.workspace().recents;
  if(const std::size_t count = build.resolvable(recents, kMaxRecentRows); count > 0) {
    if(build.section(ui::SidebarSection::Recent, "Recent", count)) {
      build.noteShortcuts(recents, kMaxRecentRows);
    }
  }

  build.finish();
  // A list of nothing but headings used to be a heading over a hole, and was
  // cleared so the empty message could say what had happened instead. It is not
  // any more: a band is a control, and four shut bands is what a reader who
  // shut them asked for. So the check is now for a list with no bands *and* no
  // rows -- which is a library with nothing in it.
  const bool onlyLabels = std::none_of(ui.sidebar.rows.begin(), ui.sidebar.rows.end(),
                                       [](const auto& row) {
                                         return row.kind != SidebarRow::Kind::SectionLabel ||
                                                row.section.has_value();
                                       });
  // The library's own root, alone, is a hole with a disclosure triangle on it:
  // it opens onto nothing and selects the folder you are already in. A fresh
  // library shows exactly that, so the "no notes yet" message -- which is also
  // where the shortcut for writing the first one is -- was written and then
  // never reachable.
  //
  // Asked of the *content* rows rather than of the whole list, because the
  // NOTEBOOKS band now sits above that row and a size check counted two.
  const auto contentRows = std::count_if(ui.sidebar.rows.begin(), ui.sidebar.rows.end(),
                                         [](const auto& row) {
                                           return row.kind != SidebarRow::Kind::SectionLabel;
                                         });
  const bool rootAlone = contentRows == 1 &&
    std::any_of(ui.sidebar.rows.begin(), ui.sidebar.rows.end(), [](const auto& row) {
      return row.kind == SidebarRow::Kind::Tree &&
             row.tree.kind == ui::TreeRowKind::Folder && row.tree.folder.empty();
    });
  if(onlyLabels || rootAlone) ui.sidebar.rows.clear();
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
void buildSidebarRows(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
  // The rows and the rectangle they were placed in are one fact, so the build
  // records it. The draw used to, one line before calling this, which made it
  // possible -- and for a while true -- for a reader of `ui.sidebar.rect` to be
  // looking at a rect no row had been placed against.
  ui.sidebar.rect = rect;
  const auto& workspace = ui.state.workspace();
  const auto& previous = ui.sidebar.rowsKey;
  const bool reusable = previous.valid &&
    previous.stateRevision == ui.state.revision() &&
    previous.treeRevision == ui.sidebar.tree.revision() &&
    previous.search == ui.fields.search.text() &&
    previous.searchScope == ui.fields.searchScope &&
    previous.tag == ui.state.selection().tag &&
    previous.favorites == workspace.favorites &&
    previous.recents == workspace.recents &&
    previous.collapsedSections == workspace.collapsedSections &&
    previous.width == rect.w &&
    previous.height == rect.h &&
    // Every row's height is derived from these, so a change of text size has to
    // rebuild the list rather than shift a list laid out at the old rhythm.
    previous.rowHeight == metrics.row &&
    previous.snippetHeight == metrics.snippet;

  if(reusable) {
    // Clamp first: the scroll the rows are shifted by has to be the one they
    // will be drawn at, or a clamp after the shift leaves them a scroll behind.
    ui.sidebar.list.setContent(rect.h - kSidebarListPadding * 2.0f, previous.contentHeight);
    const float dx = rect.x - previous.originX;
    const float dy = (rect.y - previous.originY) - static_cast<float>(ui.sidebar.list.scroll() - previous.scroll);
    if(dx != 0.0f || dy != 0.0f) {
      for(auto& row : ui.sidebar.rows) {
        row.rect.x += dx;
        row.rect.y += dy;
        row.disclosure.x += dx;
        row.disclosure.y += dy;
      }
    }
    ui.sidebar.rowsKey.originX = rect.x;
    ui.sidebar.rowsKey.originY = rect.y;
    ui.sidebar.rowsKey.scroll = ui.sidebar.list.scroll();
    perf::addCounter(perf::CounterId::SidebarRowsReused, ui.sidebar.rows.size());
    return;
  }

  rebuildSidebarRows(ui, rect, metrics);

  auto& key = ui.sidebar.rowsKey;
  key.valid = true;
  key.stateRevision = ui.state.revision();
  key.treeRevision = ui.sidebar.tree.revision();
  key.search = ui.fields.search.text();
  key.searchScope = ui.fields.searchScope;
  key.tag = ui.state.selection().tag;
  key.favorites = workspace.favorites;
  key.recents = workspace.recents;
  key.collapsedSections = workspace.collapsedSections;
  key.width = rect.w;
  key.height = rect.h;
  key.rowHeight = metrics.row;
  key.snippetHeight = metrics.snippet;
  key.originX = rect.x;
  key.originY = rect.y;
  // Read back rather than remembered: finish() clamps it.
  key.scroll = ui.sidebar.list.scroll();
}

// Scrolls the cursor row into view using last frame's geometry, which is all
// that is needed to know whether it is off an edge and by how much.
void revealSidebarRow(UiRuntime& ui, std::size_t index) {
  if(index >= ui.sidebar.rows.size() || ui.sidebar.rect.h <= 0.0f) return;
  const Rect row = ui.sidebar.rows[index].rect;
  const float top = ui.sidebar.rect.y + 8.0f;
  const float bottom = ui.sidebar.rect.y + ui.sidebar.rect.h - 8.0f;
  if(row.y < top) ui.sidebar.list.scrollBy(-static_cast<int>(std::ceil(top - row.y)));
  else if(row.y + row.h > bottom) ui.sidebar.list.scrollBy(static_cast<int>(std::ceil(row.y + row.h - bottom)));
}

void moveTreeCursor(UiRuntime& ui, int delta) {
  if(ui.sidebar.rows.empty()) return;
  int index = std::clamp(ui.sidebar.cursor, 0, static_cast<int>(ui.sidebar.rows.size()) - 1) + delta;
  // A *caption* is drawn and nothing else, so the cursor steps over it. A band
  // is a control -- it shuts the rows under it -- so the cursor stops on one,
  // and Left/Right there shuts and opens it. Without that the collapsing was
  // reachable only with a pointer.
  //
  // Landing on a band is safe because `activateSidebarRow` ignores a `Cursor`
  // activation on one: arrowing past a band must not shut it.
  while(index >= 0 && index < static_cast<int>(ui.sidebar.rows.size()) &&
        ui.sidebar.rows[static_cast<std::size_t>(index)].kind == SidebarRow::Kind::SectionLabel &&
        !ui.sidebar.rows[static_cast<std::size_t>(index)].section) {
    index += delta;
  }
  if(index < 0 || index >= static_cast<int>(ui.sidebar.rows.size())) return;
  ui.sidebar.cursor = index;
  revealSidebarRow(ui, static_cast<std::size_t>(index));
  // Cursor, not Click: arrowing through the tree shows each note it passes
  // over, and must neither unfold the library nor open a tab per note.
  activateSidebarRow(ui, ui.sidebar.rows[static_cast<std::size_t>(index)], RowActivation::Cursor);
}

// Right opens a folder, or steps into it when it is already open; Left closes
// it, or jumps to its parent when there is nothing to close.
void expandTreeCursor(UiRuntime& ui, bool open) {
  if(ui.sidebar.cursor < 0 || ui.sidebar.cursor >= static_cast<int>(ui.sidebar.rows.size())) return;
  const SidebarRow row = ui.sidebar.rows[static_cast<std::size_t>(ui.sidebar.cursor)];
  // A band shuts and opens like a folder does, which is what it looks like:
  // both wear the same chevron and both hide the rows under them.
  if(row.kind == SidebarRow::Kind::SectionLabel && row.section) {
    if(open == !ui.state.workspace().sectionCollapsed(*row.section)) {
      moveTreeCursor(ui, open ? 1 : -1);
      return;
    }
    ui.state.editWorkspace().setSectionCollapsed(*row.section, !open);
    return;
  }
  if(row.kind != SidebarRow::Kind::Tree || row.tree.kind == ui::TreeRowKind::Note) {
    if(!open) moveTreeCursor(ui, -1);
    return;
  }
  if(open == row.tree.expanded) {
    moveTreeCursor(ui, open ? 1 : -1);
    return;
  }
  if(!row.tree.expandable) return;
  ui.sidebar.tree.setExpanded(row.tree.folder, open);
}

FocusArea chooseSidebarCursorRow(UiRuntime& ui) {
  if(ui.sidebar.cursor < 0 || ui.sidebar.cursor >= static_cast<int>(ui.sidebar.rows.size())) {
    return FocusArea::Editor;
  }
  const SidebarRow row = ui.sidebar.rows[static_cast<std::size_t>(ui.sidebar.cursor)];
  activateSidebarRow(ui, row, RowActivation::Click);
  // A tag and a band both change what the list is showing rather than opening
  // anything, so the reader stays in the list to see what happened.
  const bool changedTheList = row.kind == SidebarRow::Kind::Tag ||
                              (row.kind == SidebarRow::Kind::SectionLabel && row.section);
  return changedTheList ? FocusArea::Folders : FocusArea::Editor;
}

void pressSidebarRow(UiRuntime& ui, const SidebarRow& row, float x, float y, Uint8 button) {
  // Innermost control first, all the way down. Each of these is drawn *on* the
  // row, so testing the row before any of them would mean the row swallowed
  // every one of its own controls.
  //
  // A tag dot at a note row's trailing edge is a control before it is
  // decoration: it says the note carries that tag, and clicking it filters by
  // it. It goes first because it is the smallest thing on the row.
  if(const auto tag = sidebarTagDotAt(ui, row, x, y)) {
    if(button == SDL_BUTTON_LEFT) selectTag(ui, *tag);
    else if(button == SDL_BUTTON_RIGHT) openTagMenu(ui, *tag, x, y);
    return;
  }
  // A folder's disclosure triangle opens it without making it the selection:
  // looking inside a notebook is not the same as switching to it. A band's
  // does not need a case of its own -- the whole band is its control, which
  // `activateSidebarRow` handles below.
  if(row.kind == SidebarRow::Kind::Tree && row.disclosure.w > 0.0f &&
     contains(row.disclosure, x, y) && button == SDL_BUTTON_LEFT) {
    ui.sidebar.tree.toggle(row.tree.folder);
    return;
  }
  // On a tag's own row the whole row already means that tag, so the dot on it
  // is not a separate target; a right click is where its colour comes from.
  if(row.kind == SidebarRow::Kind::Tag && button == SDL_BUTTON_RIGHT) {
    openTagMenu(ui, row.tag, x, y);
    return;
  }
  activateSidebarRow(ui, row, RowActivation::Click);
  if(button == SDL_BUTTON_RIGHT) {
    if(row.kind == SidebarRow::Kind::SearchResult) openNoteMenu(ui, x, y);
    else if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::Note) openNoteMenu(ui, x, y);
    else if(row.kind == SidebarRow::Kind::Tree) openFolderMenu(ui, x, y);
    return;
  }
  // A left press on a tree row may turn into a drag, which is decided by
  // whether the pointer moves before it is let go.
  if(button == SDL_BUTTON_LEFT && row.kind == SidebarRow::Kind::Tree) {
    if(row.tree.kind == ui::TreeRowKind::Note) {
      ui.sidebar.drag.note = true;
      ui.sidebar.drag.noteId = row.tree.noteId;
    } else if(!row.tree.folder.empty()) {
      ui.sidebar.drag.folder = true;
      ui.sidebar.drag.folderPath = row.tree.folder;
    }
  }
}

void activateSidebarRow(UiRuntime& ui, const SidebarRow& row, RowActivation how) {
  // A band is a control, and the whole band is the control -- not just the
  // chevron on it. That is what every collapsible heading does, and on a 12px
  // triangle it is the difference between a target and a game of darts.
  //
  // Click only. The keyboard cursor steps over section labels rather than
  // landing on them, so a `Cursor` activation here would mean a band shutting
  // as the reader arrowed past it.
  if(row.kind == SidebarRow::Kind::SectionLabel) {
    if(how == RowActivation::Click && row.section) ui.state.editWorkspace().toggleSection(*row.section);
    return;
  }
  // Passing over a note takes over the tab showing; asking for one gives it a
  // tab of its own.
  const auto policy = how == RowActivation::Click ? ui::TabPolicy::NewTab : ui::TabPolicy::Reuse;
  if(row.kind == SidebarRow::Kind::SearchResult) {
    selectNoteById(ui, row.noteId, policy);
    return;
  }
  if(row.kind == SidebarRow::Kind::Tag) {
    // Click only, for the reason a folder is not unfolded by being passed over
    // and a note passed over does not get a tab of its own: choosing a tag
    // replaces the **entire row list** with the notes carrying it, so a walk
    // that applied one would destroy the list it was walking -- the cursor's
    // index would then point into a different list, and the next Down would
    // land somewhere unrelated. Holding Down through the TAGS band did exactly
    // that, and with the toggle on `selectTag` a second pass over the same tag
    // turned the filter off again.
    if(how == RowActivation::Click) selectTag(ui, row.tag);
    return;
  }
  if(row.kind != SidebarRow::Kind::Tree) return;
  if(row.tree.kind == ui::TreeRowKind::Note) {
    selectNoteById(ui, row.tree.noteId, policy);
    // Opening a note moves the context to its folder *and* opens the tree onto
    // it, so the breadcrumb and the sidebar agree about where the note is. A
    // search owns the row list while it is running, so it is left alone.
    if(ui.fields.search.empty() && ui.state.selection().noteId == row.tree.noteId) {
      showFolder(ui, row.tree.folder);
    }
    return;
  }
  // A notebook has no note to open in a tab of its own; it opens the first note
  // in it, which is a move of the selection rather than opening anything new.
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectFolder(row.tree.folder);
  if(how == RowActivation::Click) ui.sidebar.tree.setExpanded(row.tree.folder, true);
  selectNoteAt(ui, 0);
}

void fillSearchSnippets(UiRuntime& ui, std::size_t index, float width,
                        const SnippetMeasure& measure) {
  if(index >= ui.sidebar.rows.size()) return;
  SidebarRow& row = ui.sidebar.rows[index];
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

std::vector<Rect> tagDotRects(Rect row, std::size_t tagCount) {
  std::vector<Rect> dots;
  if(tagCount == 0) return dots;
  const std::size_t shown = std::min(tagCount, kMaxTagDots);
  dots.reserve(shown);
  // Right to left from the trailing edge, so a note with one tag puts its dot
  // where a note with four puts its last: the column reads as a column whatever
  // is in it, rather than shifting with each row's tag count.
  float x = row.x + row.w - ui::kSpace1 - kTagDotSize;
  const float y = std::round(row.y + (row.h - kTagDotSize) / 2.0f);
  for(std::size_t i = 0; i < shown; ++i) {
    dots.push_back({std::round(x), y, kTagDotSize, kTagDotSize});
    x -= kTagDotSize + kTagDotGap;
  }
  // Back into tag order, which is the order the caller has the tags in. The
  // layout is right to left and the meaning is left to right, and keeping the
  // two apart here is what stops every caller from reversing an index.
  std::reverse(dots.begin(), dots.end());
  return dots;
}

std::optional<std::string> sidebarTagDotAt(const UiRuntime& ui, const SidebarRow& row,
                                           float x, float y) {
  if(row.kind != SidebarRow::Kind::Tree || row.tree.kind != ui::TreeRowKind::Note) return std::nullopt;
  const auto* note = ui.state.noteById(row.tree.noteId);
  if(!note || note->tags.empty()) return std::nullopt;
  // Through the same function the draw lays them out with, so the target is the
  // dot on screen. At seven pixels a second spelling of "the third from the
  // right" would be off by one somewhere and nobody would be able to say where.
  const auto dots = tagDotRects(row.rect, note->tags.size());
  for(std::size_t i = 0; i < dots.size(); ++i) {
    if(!contains(dots[i], x, y)) continue;
    // The overflow marker filters by the first tag it stands for. It is not the
    // whole truth, but a control that names four tags and then does nothing is
    // worse than one that acts on the first of them.
    return note->tags[std::min(i, note->tags.size() - 1)];
  }
  return std::nullopt;
}

std::pair<std::size_t, std::size_t> sidebarRowRange(const std::vector<SidebarRow>& rows,
                                                    float top, float bottom) {
  std::uint64_t probes = 0;
  const auto range = ui::rowBand(rows.size(), top, bottom,
                                 [&rows](std::size_t i) { return rows[i].rect; }, &probes);
  perf::addCounter(perf::CounterId::SidebarRowRangeQueries);
  perf::addCounter(perf::CounterId::SidebarRowRangeProbes, probes);
  return range;
}

// The row under the pointer, or nothing when the pointer is off the list.
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, float x, float y) {
  // The list rect the draw recorded, not one the caller works out. It used to
  // be a parameter, and three of the four callers passed `sidebarListRect(...)`
  // while the fourth -- the drop at the end of a note drag -- passed the whole
  // panel. Nothing went visibly wrong, because the rows only occupy the list
  // and a `y` in the search box above them matches none; but the drop was
  // hit-testing a different rect from the one that had highlighted the row
  // under the pointer on the way there, for no reason anybody chose.
  if(!contains(ui.sidebar.rect, x, y)) return std::nullopt;
  // A band of zero height is the rows the pointer's y is inside. Rows tile, so
  // a pointer exactly on a boundary is inside two of them and the first one
  // wins -- which is the answer the walk gave.
  const auto [begin, end] = sidebarRowRange(ui.sidebar.rows, y, y);
  for(std::size_t i = begin; i < end; ++i) {
    const auto& row = ui.sidebar.rows[i];
    // A caption is drawn and nothing else -- a result count and the name of the
    // tag being filtered by are not controls. A *band* is: it shuts the rows
    // under it, so it has to be findable under the pointer. This used to skip
    // every section label, which is why the earlier headings could not have
    // been made clickable without also making the captions clickable.
    if(row.kind == SidebarRow::Kind::SectionLabel && !row.section) continue;
    if(contains(row.rect, x, y)) return i;
  }
  return std::nullopt;
}

}
