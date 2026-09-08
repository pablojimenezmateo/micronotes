#include "app/SidebarModel.h"

#include "app/ContextMenus.h"
#include "app/Notes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/RowBand.h"
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
  if(const auto* results = ui.searchResults.get(key)) return *results;
  return ui.searchResults.store(key, ui.state.currentSearchResults());
}

namespace {

// The row list, from the library. O(library) rather than O(viewport): the tree
// relativises a path and builds a map key for every note before it can place
// the first row, and every row is a string, a path and a rect. Called through
// buildSidebarRows(), which is what keeps it off the frame path.
void rebuildSidebarRows(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
  ui.sidebarRows.clear();
  const float top = rect.y + 12.0f;
  float y = top - static_cast<float>(ui.sidebarScroll);

  // A caption on what the list is showing: the result count, the tag being
  // filtered by. It heads the list the same way a band does and reads the same,
  // but there is nothing under it to shut, so it gets no chevron.
  //
  // Title case throughout, not caps. microide's sections are "Staged" and
  // "Unstaged", and at 13px the reason is legible: uppercase in a mono face is
  // a row of same-width rectangles with no ascenders or descenders to tell them
  // apart, so a caps heading reads as a texture before it reads as a word.
  const auto pushCaption = [&](std::string label) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    // Full panel width, unlike the rows it heads. A band that stops short of
    // the panel edges reads as a card sitting in the list rather than as a
    // division of it, and a division is the whole point.
    row.rect = {rect.x, y, rect.w, metrics.label};
    ui.sidebarRows.push_back(std::move(row));
    y += metrics.label;
  };
  // A band that can be shut. Returns whether to go on and emit its contents,
  // so a collapsed section is one `if` at the call site rather than a branch
  // around each of the four groups.
  const auto pushSection = [&](ui::SidebarSection section, std::string label, std::size_t count) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    row.section = section;
    row.collapsed = ui.state.workspace().sectionCollapsed(section);
    // The count is worth most on a band that is shut, which is the one case
    // where what is under it cannot be counted by looking.
    if(count > 0) row.trailing = std::to_string(count);
    row.rect = {rect.x, y, rect.w, metrics.label};
    // The same control a folder row wears, at the panel's own inset rather
    // than on the label column the rows use. That outdent is the visual
    // difference between a heading and a row -- see `ui::kSectionLabelX`.
    row.disclosure = {rect.x + ui::kSectionChevronX,
                      y + (metrics.label - kSidebarGutterWidth) / 2.0f,
                      kSidebarGutterWidth, kSidebarGutterWidth};
    const bool collapsed = row.collapsed;
    ui.sidebarRows.push_back(std::move(row));
    y += metrics.label;
    return !collapsed;
  };
  const auto pushTreeRow = [&](ui::TreeRow tree) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tree;
    row.rect = {rect.x + ui::kSpace2, y, rect.w - ui::kSpace2 * 2.0f, metrics.row};
    if(tree.expandable) {
      row.disclosure = {row.rect.x + kSidebarGutterX + static_cast<float>(tree.depth) * kSidebarIndent,
                        y + (metrics.row - kSidebarGutterWidth) / 2.0f,
                        kSidebarGutterWidth, kSidebarGutterWidth};
    }
    row.tree = std::move(tree);
    ui.sidebarRows.push_back(std::move(row));
    y += metrics.row;
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
    pushCaption(std::to_string(results.size()) + (results.size() == 1 ? " result" : " results"));
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
      row.rect = {rect.x + ui::kSpace2, y, rect.w - ui::kSpace2 * 2.0f,
                  searchResultRowHeight(row.matchLineCount, metrics)};
      y += row.rect.h;
      ui.sidebarRows.push_back(std::move(row));
    }
    finish();
    return;
  }

  // A tag is a filter over the library, so choosing one lists what carries it
  // instead of the tree it cuts across. No `#`: the sidebar draws a tag's own
  // colour beside its name everywhere else, and a sigil as well is the same
  // thing said twice in two registers.
  if(!ui.state.selection().tag.empty()) {
    pushCaption(ui.state.selection().tag);
    for(const auto& note : notes) {
      if(std::find(note.tags.begin(), note.tags.end(), ui.state.selection().tag) == note.tags.end()) continue;
      pushFlatNote(note);
    }
    finish();
    return;
  }

  // How many of a shortcut list still name a note. Counted before the band is
  // pushed rather than after, because a band that can be shut has to carry its
  // count whether or not its rows were emitted -- and because a heading over
  // nothing at all is a lie whichever way round it is built. This is the
  // `resize` the two shortcut lists used to do to take a heading back.
  const auto resolvable = [&](const std::vector<std::string>& ids, std::size_t limit) {
    std::size_t found = 0;
    for(const auto& id : ids) {
      if(found >= limit) break;
      if(ui.state.noteById(id)) ++found;
    }
    return found;
  };

  const auto& favorites = ui.state.workspace().favorites;
  if(const std::size_t count = resolvable(favorites, kMaxFavoriteRows); count > 0) {
    if(pushSection(ui::SidebarSection::Favorites, "Favorites", count)) {
      pushNoteShortcuts(favorites, kMaxFavoriteRows);
    }
  }

  // The tree is a band like the other three. It had no heading at all, which is
  // most of what made the divisions unclear: an unlabelled group between two
  // labelled ones reads as the tail of the one above it.
  auto treeRows = ui.tree.rows(ui.state.folders(), notes);
  if(!treeRows.empty()) {
    if(pushSection(ui::SidebarSection::Notebooks, "Notebooks", notes.size())) {
      for(auto& row : treeRows) pushTreeRow(std::move(row));
    }
  }

  const auto& tags = ui.state.tags();
  if(!tags.empty()) {
    // Tags are a filter over the tree, not a second way to organise it, so they
    // sit below it.
    if(pushSection(ui::SidebarSection::Tags, "Tags", tags.size())) {
      for(const auto& tag : tags) {
        SidebarRow row;
        row.kind = SidebarRow::Kind::Tag;
        row.rect = {rect.x + ui::kSpace2, y, rect.w - ui::kSpace2 * 2.0f, metrics.tag};
        row.tag = tag;
        ui.sidebarRows.push_back(std::move(row));
        y += metrics.tag;
      }
    }
  }

  const auto& recents = ui.state.workspace().recents;
  if(const std::size_t count = resolvable(recents, kMaxRecentRows); count > 0) {
    if(pushSection(ui::SidebarSection::Recent, "Recent", count)) {
      pushNoteShortcuts(recents, kMaxRecentRows);
    }
  }

  finish();
  // A list of nothing but headings used to be a heading over a hole, and was
  // cleared so the empty message could say what had happened instead. It is not
  // any more: a band is a control, and four shut bands is what a reader who
  // shut them asked for. So the check is now for a list with no bands *and* no
  // rows -- which is a library with nothing in it.
  const bool onlyLabels = std::none_of(ui.sidebarRows.begin(), ui.sidebarRows.end(),
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
  const auto contentRows = std::count_if(ui.sidebarRows.begin(), ui.sidebarRows.end(),
                                         [](const auto& row) {
                                           return row.kind != SidebarRow::Kind::SectionLabel;
                                         });
  const bool rootAlone = contentRows == 1 &&
    std::any_of(ui.sidebarRows.begin(), ui.sidebarRows.end(), [](const auto& row) {
      return row.kind == SidebarRow::Kind::Tree &&
             row.tree.kind == ui::TreeRowKind::Folder && row.tree.folder.empty();
    });
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
void buildSidebarRows(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
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

  rebuildSidebarRows(ui, rect, metrics);

  auto& key = ui.sidebarRowsKey;
  key.valid = true;
  key.stateRevision = ui.state.revision();
  key.treeRevision = ui.tree.revision();
  key.search = ui.search.text();
  key.searchScope = ui.searchScope;
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
  key.scroll = ui.sidebarScroll;
}

// Scrolls the cursor row into view using last frame's geometry, which is all
// that is needed to know whether it is off an edge and by how much.
void revealSidebarRow(UiRuntime& ui, std::size_t index) {
  if(index >= ui.sidebarRows.size() || ui.sidebarRect.h <= 0.0f) return;
  const Rect row = ui.sidebarRows[index].rect;
  const float top = ui.sidebarRect.y + 8.0f;
  const float bottom = ui.sidebarRect.y + ui.sidebarRect.h - 8.0f;
  if(row.y < top) ui.sidebarScroll -= static_cast<int>(std::ceil(top - row.y));
  else if(row.y + row.h > bottom) ui.sidebarScroll += static_cast<int>(std::ceil(row.y + row.h - bottom));
  ui.sidebarScroll = std::clamp(ui.sidebarScroll, 0, ui.sidebarMaxScroll);
}

void moveTreeCursor(UiRuntime& ui, int delta) {
  if(ui.sidebarRows.empty()) return;
  int index = std::clamp(ui.folderCursor, 0, static_cast<int>(ui.sidebarRows.size()) - 1) + delta;
  // A *caption* is drawn and nothing else, so the cursor steps over it. A band
  // is a control -- it shuts the rows under it -- so the cursor stops on one,
  // and Left/Right there shuts and opens it. Without that the collapsing was
  // reachable only with a pointer.
  //
  // Landing on a band is safe because `activateSidebarRow` ignores a `Cursor`
  // activation on one: arrowing past a band must not shut it.
  while(index >= 0 && index < static_cast<int>(ui.sidebarRows.size()) &&
        ui.sidebarRows[static_cast<std::size_t>(index)].kind == SidebarRow::Kind::SectionLabel &&
        !ui.sidebarRows[static_cast<std::size_t>(index)].section) {
    index += delta;
  }
  if(index < 0 || index >= static_cast<int>(ui.sidebarRows.size())) return;
  ui.folderCursor = index;
  revealSidebarRow(ui, static_cast<std::size_t>(index));
  // Cursor, not Click: arrowing through the tree shows each note it passes
  // over, and must neither unfold the library nor open a tab per note.
  activateSidebarRow(ui, ui.sidebarRows[static_cast<std::size_t>(index)], RowActivation::Cursor);
}

// Right opens a folder, or steps into it when it is already open; Left closes
// it, or jumps to its parent when there is nothing to close.
void expandTreeCursor(UiRuntime& ui, bool open) {
  if(ui.folderCursor < 0 || ui.folderCursor >= static_cast<int>(ui.sidebarRows.size())) return;
  const SidebarRow row = ui.sidebarRows[static_cast<std::size_t>(ui.folderCursor)];
  // A band shuts and opens like a folder does, which is what it looks like:
  // both wear the same chevron and both hide the rows under them.
  if(row.kind == SidebarRow::Kind::SectionLabel && row.section) {
    if(open == !ui.state.workspace().sectionCollapsed(*row.section)) {
      moveTreeCursor(ui, open ? 1 : -1);
      return;
    }
    ui.state.workspace().setSectionCollapsed(*row.section, !open);
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
  ui.tree.setExpanded(row.tree.folder, open);
}

FocusArea chooseSidebarCursorRow(UiRuntime& ui) {
  if(ui.folderCursor < 0 || ui.folderCursor >= static_cast<int>(ui.sidebarRows.size())) {
    return FocusArea::Editor;
  }
  const SidebarRow row = ui.sidebarRows[static_cast<std::size_t>(ui.folderCursor)];
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
    ui.tree.toggle(row.tree.folder);
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
      ui.draggingNote = true;
      ui.draggingNoteId = row.tree.noteId;
    } else if(!row.tree.folder.empty()) {
      ui.draggingFolder = true;
      ui.draggingFolderPath = row.tree.folder;
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
    if(how == RowActivation::Click && row.section) ui.state.workspace().toggleSection(*row.section);
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
    if(ui.search.empty() && ui.state.selection().noteId == row.tree.noteId) {
      showFolder(ui, row.tree.folder);
    }
    return;
  }
  // A notebook has no note to open in a tab of its own; it opens the first note
  // in it, which is a move of the selection rather than opening anything new.
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectFolder(row.tree.folder);
  if(how == RowActivation::Click) ui.tree.setExpanded(row.tree.folder, true);
  selectNoteAt(ui, 0);
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
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, Rect sidebar, float x, float y) {
  if(!contains(sidebar, x, y)) return std::nullopt;
  // A band of zero height is the rows the pointer's y is inside. Rows tile, so
  // a pointer exactly on a boundary is inside two of them and the first one
  // wins -- which is the answer the walk gave.
  const auto [begin, end] = sidebarRowRange(ui.sidebarRows, y, y);
  for(std::size_t i = begin; i < end; ++i) {
    const auto& row = ui.sidebarRows[i];
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
