#pragma once

#include "CoreAliases.h"

#include "app/Wheel.h"
#include "library/Library.h"
#include "ui/Memo.h"
#include "ui/Rect.h"
#include "library/SearchScope.h"
#include "ui/TextFit.h"
#include "ui/TreeModel.h"
#include "ui/WorkspaceModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The panel to the left of the page: what the library contains.
//
// Nineteen loose fields on `UiRuntime` before this, which is roughly a seventh
// of it, and they answered four different questions -- what rows exist, where
// the list is scrolled to, what is being dragged onto what, and what the search
// box is asking. Named together, the one non-obvious thing about the panel
// becomes visible: the tree and the tag filter and the search results are *one*
// scrolling list, so there is one geometry to draw, one to hit-test and one
// thing to scroll, and every question about the panel is a question about
// `rows`.
namespace micronotes::app {

using micronotes::ui::Rect;

// One drawn line of the sidebar. The tree and the tag filter share a single
// scrolling list, so there is one geometry to draw, one to hit-test, and one
// thing to scroll.
struct SidebarRow {
  enum class Kind {
    Tree,
    // A band across the panel heading the rows under it. Two sorts: one that
    // names a `SidebarSection` and can be shut, and one that is a caption on
    // what the list is currently showing -- the result count, the tag being
    // filtered by -- which has nothing to shut.
    SectionLabel,
    Tag,
    // A note that matched the query, with the lines that matched under it.
    // While a search is running these replace the tree rather than appearing
    // beside them: the sidebar answers one question at a time.
    SearchResult
  };

  Kind kind = Kind::Tree;
  Rect rect;
  std::string label;   // section labels only
  // Section labels only: which band this is, when it is one the reader can
  // shut. Absent on a caption, which is what tells the draw not to give it a
  // chevron and the click not to look for one.
  std::optional<ui::SidebarSection> section;
  bool collapsed = false;
  // Drawn right-aligned in a band: how many rows are under it. It is worth
  // most on a band that is *shut*, which is the one case where the rows cannot
  // be counted by looking.
  std::string trailing;
  // A folder row with something inside it, and a band that can be shut, both
  // get one -- they are the same control doing the same job. An empty rect
  // means the whole row acts rather than expanding.
  Rect disclosure;
  ui::TreeRow tree;
  std::string tag;
  // Search results only.
  std::string noteId;
  std::string title;
  // The matching lines alone, without the context either side. A sidebar-width
  // column has no room for three lines per hit, and showing them would triple
  // every row's height for text nobody can read at that width.
  //
  // Already trimmed to the column, and carrying where the match sits inside
  // what is left, so the draw marks the span and does not have to measure the
  // line again on every frame. `length` is zero when there is nothing to mark
  // -- a note whose title matched but whose text did not.
  //
  // Filled on the first frame the row is *drawn*, not when the list is built.
  // Trimming one matching line to the column measures the whole line and then
  // searches, with a measurement per probe, and every probe is a string nothing
  // has measured before -- so the measure cache cannot help and each is a real
  // shaping pass. A 200-result query has 600 of them, and building all of them
  // up front made the first frame of a query a 150-500 ms freeze, per keystroke
  // of the query.
  // The row list stays O(results), because the heights have to add up to a
  // scrollbar; the trimming is O(viewport).
  std::vector<ui::SnippetWindow> matchLines;
  // Which result this row lists, and how many lines it will show once they are
  // trimmed. The count is known without measuring anything, which is what lets
  // the row take its final height before its text exists.
  std::size_t resultIndex = 0;
  std::size_t matchLineCount = 0;
  bool matchLinesBuilt = false;
};


// What the row list was built from. The list is a pure function of these, and
// it used to be rebuilt on every frame -- a relativised path, a map key and a
// row object per note in the library, to draw the three dozen rows the panel is
// tall enough to show. Only the scroll and the panel origin move on a typical
// frame, and both are an offset applied to a list already built.
//
// Not a `ui::Memo`, and this is the one memo in the shell that should not be:
// its reuse is *partial*. The fields above the line decide whether the rows
// exist at all; the ones below only decide where they sit, and a change to
// those shifts every rect by a delta instead of rebuilding anything. A memo
// with one key cannot express that, and pretending it could would turn a scroll
// into an O(library) rebuild.
struct SidebarRowsKey {
  bool valid = false;
  std::uint64_t stateRevision = 0;
  std::uint64_t treeRevision = 0;
  std::string search;
  library::SearchScope searchScope = library::SearchScope::All;
  std::string tag;
  std::vector<std::string> favorites;
  std::vector<std::string> recents;
  // Which bands are shut. Every row below a shut band moves, so this is
  // geometry like the panel's width is -- and a key rather than a flag, because
  // a flag has to be raised at every site that shuts a band, and the one that
  // forgets leaves the panel hit-testing rows it is no longer drawing.
  std::array<bool, 4> collapsedSections {};
  float width = 0.0f;
  float height = 0.0f;
  // The rhythm the rows were laid out at. Every height in the list derives from
  // these two, so a change of text size has to rebuild rather than shift.
  float rowHeight = 0.0f;
  float snippetHeight = 0.0f;

  // ---- placement only, from here down: a change shifts the rows ----
  float originX = 0.0f;
  float originY = 0.0f;
  int scroll = 0;
  float contentHeight = 0.0f;
};

// The question the result list answers, and the library it answered it against.
struct SearchKey {
  std::string query;
  library::SearchScope scope = library::SearchScope::All;
  std::uint64_t libraryRevision = 0;
  bool operator==(const SearchKey&) const = default;
};

// A note or a notebook being dragged onto a row of the tree.
//
// One struct because the two are one gesture with one drop target, and as five
// loose fields the "which of these two is in flight" question was asked as
// `draggingNote || draggingFolder` at six sites -- each of which had to
// remember that both can be false and neither can be true at once.
struct SidebarDrag {
  bool note = false;
  std::string noteId;
  bool folder = false;
  std::filesystem::path folderPath;
  // The row under the pointer, highlighted rather than merely guessed at.
  std::optional<std::size_t> dropRow;

  bool active() const { return note || folder; }
  void clear() { *this = {}; }
};

struct SidebarState {
  // Which folders are open. A view preference, kept out of the library: a
  // disclosure triangle must not touch a file.
  ui::TreeModel tree;

  // The one list: the tree, the tag filter and the search results all become
  // rows in it.
  std::vector<SidebarRow> rows;
  SidebarRowsKey rowsKey;
  // The results being listed. Held rather than re-queried because the row list
  // is rebuilt on every frame -- including the ones a hover causes -- and each
  // query is a hit on SQLite.
  ui::Memo<std::vector<library::SearchResult>, SearchKey> searchResults;

  // Last frame's rect, so keyboard navigation can scroll a row into view
  // without recomputing the whole window layout.
  Rect rect;
  ScrollList list;
  // What the last draw kept clear at the trailing edge of every row: the
  // scrollbar's lane when one was showing, and zero when the list fitted. See
  // `ui::scrollbarReserve`.
  //
  // Recorded by the draw and read by the hit tests, which is the same
  // discipline `ui.tabStrip` follows and for the same reason: the number
  // depends on whether a bar is showing *this* frame, so a hit test that
  // recomputed it would be answering for a panel that had not been painted
  // yet. A tag dot is seven pixels wide; the paint and the tooltip disagreeing
  // by fourteen would put every dot's name on its neighbour.
  float trailingReserve = 0.0f;
  // Which row the keyboard is on, as an index into `rows`.
  int cursor = 0;

  SidebarDrag drag;
  // Whether the notebook prompt that is open is creating one or renaming one.
  bool creatingFolder = false;
  // Whether the pointer is dragging the panel's trailing edge.
  bool resizing = false;
  // The search box's scope button, recorded as it is drawn.
  Rect scopeToggle;
};

}
