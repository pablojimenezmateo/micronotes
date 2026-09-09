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
const SidebarSearch& searchResults(UiRuntime& ui) {
  const auto& selection = ui.state.selection();
  const SearchKey key {selection.search, selection.searchScope, ui.state.catalog().revision()};
  if(const auto* results = ui.sidebar.searchResults.get(key)) return *results;
  SidebarSearch results;
  results.notes = ui.state.currentSearchResults();
  results.files = ui.state.catalog().searchCompanions(selection.search, selection.searchScope);
  return ui.sidebar.searchResults.store(key, std::move(results));
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

  // A companion file listed outside the tree, under a query. The tree's own
  // `File` row, at depth 0, so a hit opens, drags and answers a right click the
  // way the same file does in the tree.
  void fileRow(const library::CompanionEntry& entry) {
    ui::TreeRow tree;
    tree.kind = ui::TreeRowKind::File;
    tree.depth = 0;
    tree.folder = entry.folder;
    tree.file = entry.path;
    tree.label = entry.path.filename().generic_string();
    treeRow(std::move(tree));
  }

  void noteShortcuts(const std::vector<std::string>& ids, std::size_t limit) {
    std::size_t drawn = 0;
    for(const auto& id : ids) {
      if(drawn >= limit) break;
      // By id index rather than by scan. Thirteen shortcuts against a thousand
      // notes was thirteen thousand string compares to draw thirteen rows.
      const auto* found = ui_.state.catalog().noteById(id);
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
      if(ui_.state.catalog().noteById(id)) ++found;
    }
    return found;
  }

  // Whatever the list ended up holding, it scrolls the same way.
  void finish() {
    perf::addCounter(perf::CounterId::SidebarRowsBuilt, ui_.sidebar.rows.size());
    const float contentHeight = cursor_.height();
    ui_.sidebar.rowsKey.placement.contentHeight = contentHeight;
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

  const auto& notes = ui.state.catalog().notes();

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
    const auto& hits = results.notes;
    const auto& files = results.files;
    if(!hits.empty()) {
      build.caption(std::to_string(hits.size()) + (hits.size() == 1 ? " result" : " results"));
      for(std::size_t i = 0; i < hits.size(); ++i) build.searchResult(hits[i], i);
    }
    // The files whose name matched, under their own caption so a PDF called
    // `plan.pdf` is not mistaken for a note called `plan`. Only when there are
    // any: a heading over nothing is the thing the empty-result rule above is
    // there to prevent.
    if(!files.empty()) {
      build.caption(std::to_string(files.size()) + (files.size() == 1 ? " file" : " files"));
      for(const auto& entry : files) build.fileRow(entry);
    }
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
  auto treeRows = ui.sidebar.tree.rows(ui.state.catalog().folders(), notes,
                                       ui.state.catalog().companions());
  if(!treeRows.empty()) {
    if(build.section(ui::SidebarSection::Notebooks, "Notebooks", notes.size())) {
      for(auto& row : treeRows) build.treeRow(std::move(row));
    }
  }

  const auto& tags = ui.state.catalog().tags();
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
// The inputs the row list is a pure function of, gathered in one place.
//
// Both the reuse test and the store go through this, which is what makes them
// one list rather than two that have to be kept in step by hand.
SidebarRowsShape sidebarRowsShape(const UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
  const auto& workspace = ui.state.workspace();
  SidebarRowsShape shape;
  shape.stateRevision = ui.state.catalog().revision();
  shape.treeRevision = ui.sidebar.tree.revision();
  shape.search = ui.fields.search.text();
  shape.searchScope = ui.fields.searchScope;
  shape.tag = ui.state.selection().tag;
  shape.favorites = workspace.favorites;
  shape.recents = workspace.recents;
  shape.collapsedSections = workspace.collapsedSections;
  shape.width = rect.w;
  shape.height = rect.h;
  // Every row's height derives from these, so a change of text size has to
  // rebuild the list rather than shift one laid out at the old rhythm.
  shape.rowHeight = metrics.row;
  shape.snippetHeight = metrics.snippet;
  return shape;
}

void buildSidebarRows(UiRuntime& ui, Rect rect, const SidebarMetrics& metrics) {
  // The rows and the rectangle they were placed in are one fact, so the build
  // records it. The draw used to, one line before calling this, which made it
  // possible -- and for a while true -- for a reader of `ui.sidebar.rect` to be
  // looking at a rect no row had been placed against.
  ui.sidebar.rect = rect;
  const auto& previous = ui.sidebar.rowsKey;
  const SidebarRowsShape shape = sidebarRowsShape(ui, rect, metrics);
  const bool reusable = previous.valid && previous.shape == shape;

  if(reusable) {
    // Clamp first: the scroll the rows are shifted by has to be the one they
    // will be drawn at, or a clamp after the shift leaves them a scroll behind.
    ui.sidebar.list.setContent(rect.h - kSidebarListPadding * 2.0f, previous.placement.contentHeight);
    const float dx = rect.x - previous.placement.originX;
    const float dy = (rect.y - previous.placement.originY) - static_cast<float>(ui.sidebar.list.scroll() - previous.placement.scroll);
    if(dx != 0.0f || dy != 0.0f) {
      for(auto& row : ui.sidebar.rows) {
        row.rect.x += dx;
        row.rect.y += dy;
        row.disclosure.x += dx;
        row.disclosure.y += dy;
      }
    }
    ui.sidebar.rowsKey.placement.originX = rect.x;
    ui.sidebar.rowsKey.placement.originY = rect.y;
    ui.sidebar.rowsKey.placement.scroll = ui.sidebar.list.scroll();
    perf::addCounter(perf::CounterId::SidebarRowsReused, ui.sidebar.rows.size());
    return;
  }

  rebuildSidebarRows(ui, rect, metrics);

  auto& key = ui.sidebar.rowsKey;
  key.valid = true;
  // The same value the reuse test above compared against, stored whole.
  key.shape = shape;
  key.placement.originX = rect.x;
  key.placement.originY = rect.y;
  // Read back rather than remembered: finish() clamps it.
  key.placement.scroll = ui.sidebar.list.scroll();
}

void fillSearchSnippets(UiRuntime& ui, std::size_t index, float width,
                        const SnippetMeasure& measure) {
  if(index >= ui.sidebar.rows.size()) return;
  SidebarRow& row = ui.sidebar.rows[index];
  if(row.kind != SidebarRow::Kind::SearchResult || row.matchLinesBuilt) return;
  row.matchLinesBuilt = true;
  const auto& results = searchResults(ui).notes;
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

std::vector<Rect> tagDotRects(Rect row, std::size_t tagCount, float trailingReserve) {
  std::vector<Rect> dots;
  if(tagCount == 0) return dots;
  const std::size_t shown = std::min(tagCount, kMaxTagDots);
  dots.reserve(shown);
  // Right to left from the trailing edge, so a note with one tag puts its dot
  // where a note with four puts its last: the column reads as a column whatever
  // is in it, rather than shifting with each row's tag count.
  float x = row.x + row.w - trailingReserve - ui::kSpace1 - kTagDotSize;
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
  const auto* note = ui.state.catalog().noteById(row.tree.noteId);
  if(!note || note->tags.empty()) return std::nullopt;
  // Through the same function the draw lays them out with, so the target is the
  // dot on screen. At seven pixels a second spelling of "the third from the
  // right" would be off by one somewhere and nobody would be able to say where.
  const auto dots = tagDotRects(row.rect, note->tags.size(), ui.sidebar.trailingReserve);
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
