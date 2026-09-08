#pragma once

#include "app/Shell.h"
#include "ui/Metrics.h"
#include "ui/Rect.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <string_view>

// The sidebar's row list: what rows exist, where each one sits, and which one
// is under the pointer. Split out of Application.cpp because it is a model
// rather than a paint -- it reads the library and produces geometry, touches no
// renderer, and is the one part of the sidebar that has a cache to get wrong.
//
// The list is O(library) to build and O(viewport) to draw: the tree relativises
// a path and builds a map key for every note in the library before it can place
// the first row. It used to be rebuilt on every frame, which on a 400-note
// library was most of the frame. buildSidebarRows() is the memo in front of it.
//
// The build produces geometry only. Anything that costs a text measurement --
// which for this panel means trimming a search snippet to its column -- happens
// per *drawn* row instead, because the list is the length of the result set and
// the viewport is three dozen rows.
namespace micronotes::app {

// The tree is drawn as a flat list: one row height, one indent per level, and
// the nesting carried entirely by that indent.
//
// Entirely -- there are no guide hairlines down the levels any more. They were
// there to say which folder a row six levels down belonged to, which is what
// the indent and the chevron above it already say; a column of rules down a
// panel reads as a table, and the tree is not one.
inline constexpr float kSidebarIndent = ui::kTreeIndentWidth;

// Left to right inside a row: the slot that holds a folder's chevron or a
// note's icon, then the label.
//
// One pair of numbers for the whole list. A section heading, a tag, a search
// result and a tree row are four kinds of row in one column, and they used to
// start their text at four different x -- 18, 20, 20 and 28 -- because each was
// nudged into place on its own. Nothing in the list lined up with anything else.
inline constexpr float kSidebarGutterX = ui::kSidebarInset;
inline constexpr float kSidebarGutterWidth = ui::kTreeChevronSlotWidth;
inline constexpr float kSidebarLabelX = kSidebarGutterX + kSidebarGutterWidth + ui::kTreeLabelGap;

// The padding above the first row and below the last, and therefore the height
// the list loses out of the panel. `rebuildSidebarRows` starts the first row at
// `rect.y + kSidebarListPadding`, and the ceiling has to take the same amount
// off the bottom or the last row cannot be scrolled clear of the edge. It was a
// bare `24.0f` at the two ceiling sites and a bare `12.0f` at the row origin,
// which is the same number written twice in two forms.
inline constexpr float kSidebarListPadding = ui::kSpace3;

// How many rows the two shortcut lists show. A shortcut list is a way back to
// something recent, not a second library: past a handful it stops being faster
// to read than the tree it sits above. Named because the count on the band has
// to be the count of what is under it, and those were two numbers written out
// at two sites.
inline constexpr std::size_t kMaxFavoriteRows = 8;
inline constexpr std::size_t kMaxRecentRows = 5;

// The coloured dots at the trailing edge of a note's row, one per tag.
//
// This is what connects the two halves of the sidebar. A note's row named its
// folder and said nothing about its tags; the TAGS band listed tags and said
// nothing about which notes carried them. So the one way of organising a
// library that cuts across the tree was invisible from the tree.
//
// Capped, because a row is one line tall and a note with nine tags would push
// its own name off the panel. Past the cap the last dot is a marker rather than
// a tag, and its tooltip names the ones that did not fit -- hiding them
// silently would be worse than not drawing any.
inline constexpr std::size_t kMaxTagDots = 4;
inline constexpr float kTagDotSize = 7.0f;
inline constexpr float kTagDotGap = 4.0f;
// What a row with dots reserves at its trailing edge, so a long note title is
// ellipsized before it reaches them.
inline constexpr float kTagDotColumnWidth =
  static_cast<float>(kMaxTagDots) * (kTagDotSize + kTagDotGap) + ui::kSpace1;

// Where row `row`'s tag dots go, in the order the tags are listed, and at most
// `kMaxTagDots` of them. Laid out right to left from the row's trailing edge.
//
// One function for the draw and the two hit tests -- what the pointer is over
// and what a click chose -- because a dot is 7 pixels wide and three
// independent spellings of "the third dot from the right" is three chances to
// be off by one, on a target that small.
std::vector<ui::Rect> tagDotRects(ui::Rect row, std::size_t tagCount);

// The tag whose dot is under the pointer on `row`, or nothing.
//
// Answers for the overflow marker too: it stands for the tags past the cap, and
// the first of those is the one it filters by, because a marker that names four
// tags in a tooltip and then filters by none is a marker that looks broken.
std::optional<std::string> sidebarTagDotAt(const UiRuntime& ui, const SidebarRow& row,
                                           float x, float y);

// The sidebar's vertical rhythm.
//
// Every row holds a line of text, and text grows with the reader's text size
// while chrome stays put -- so a row nailed to 20 pixels was a row that, at the
// large size, drew its label into the row beneath it. The floors are what the
// default mono chrome size gives, which is where the tree's density comes from:
// a 20px row against the 26px this replaced fits a third more of the library on
// screen, and that is most of what makes a tree feel like an IDE's.
//
// Taken as line heights rather than as a renderer, so the model stays free of
// the font: the draw measures, this decides, and both are testable apart.
struct SidebarMetrics {
  float row = ui::kSidebarRowHeight;
  float tag = ui::kSidebarRowHeight;
  float label = 26.0f;
  float resultTitle = ui::kSidebarRowHeight;
  float snippet = 15.0f;
};

SidebarMetrics sidebarMetrics(int uiLineHeight, int snippetLineHeight);

float searchResultRowHeight(std::size_t matchLines, const SidebarMetrics& metrics);

// The width of a snippet line, in the font the rows draw it in.
using SnippetMeasure = std::function<int(std::string_view)>;

// The results the sidebar is listing, recomputed only when the question or the
// library has changed. Each query is a hit on SQLite.
const std::vector<library::SearchResult>& searchResults(UiRuntime& ui);

// Fills `ui.sidebar.rows` for a list occupying `rect`, and records that rect as
// the one the rows were placed against, reusing the standing list
// when nothing it depends on has moved. An empty result means the list holds
// nothing worth drawing -- the caller draws its empty message instead, and
// hit-testing finds nothing, which is the same answer.
//
// Measures nothing: the build is geometry, and the only text measurement the
// sidebar does is trimming a search snippet to its column, which now happens per
// drawn row through `fillSearchSnippets` below.
void buildSidebarRows(UiRuntime& ui, ui::Rect rect, const SidebarMetrics& metrics);

// Trims row `index`'s matching lines to the column, once. Called by the draw for
// the rows it is about to paint rather than by the build for all of them:
// trimming one line measures it and then searches with a measurement per probe,
// and every probe is a string nothing has measured before, so the measure cache
// cannot help. A 200-result query carries 600 of them -- which was 150 ms and
// up in the first frame after a keystroke in the search box, to produce the
// three dozen lines a sidebar can show.
//
// `sidebar.snippet_measures` against `sidebar.snippets_trimmed` is what one
// costs: it was around eighteen shaping passes a snippet when both searches
// bisected down from the whole line, and is six now that they estimate the
// answer from the full-line measurement and confirm it.
//
// The row keeps its own trimmed lines, so scrolling back over a row does not
// trim it again, and the row list still holds every result because the heights
// have to add up to a scrollbar.
void fillSearchSnippets(UiRuntime& ui, std::size_t index, float width,
                        const SnippetMeasure& measure);

// Half-open range of row indices whose boxes intersect the vertical band
// [top, bottom] in list space.
//
// Rows tile the list: every push advances the cursor by exactly the row's own
// height, so `rect.y` is non-decreasing and so is `rect.y + rect.h`. That makes
// the band two binary searches -- the same shape, and for the same reason, as
// `DocumentLayout::blockRange` is for the page.
//
// Both readers of the row list go through here, and that is the point of it
// existing. The draw walked all of them and tested each against the list rect;
// `sidebarRowAt` walked all of them and tested each against the pointer, on
// every mouse-motion event -- a hundred a second while the cursor crosses the
// panel. Both were O(library) to answer a question about a dozen rows, and a
// list that is sorted by `rect.y` for one reader and scanned linearly by the
// other is a list whose readers disagree about what it is.
std::pair<std::size_t, std::size_t> sidebarRowRange(const std::vector<SidebarRow>& rows,
                                                    float top, float bottom);

// The row under the pointer, or nothing when the pointer is off the list.
//
// Against the list rect the draw recorded (`ui.sidebar.rect`) rather than one
// handed in, for the same reason the tab strip stopped taking a rect: four
// callers each deciding which rectangle the rows live in is four chances to
// pick a different one, and one of them did.
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, float x, float y);

// How a sidebar row was reached. It decides two things that move together, and
// which used to be two separate booleans passed side by side:
//
//   * a notebook opens when you *click* it, and only becomes the selection when
//     you arrow onto it -- holding Down would otherwise unfold the whole
//     library on the way past;
//   * a note gets a tab of its own when you click it, and takes over the tab
//     showing when you arrow onto it -- holding Down would otherwise open a
//     tab per note in the library.
//
// Both distinctions are the same distinction: did the reader ask for this note,
// or are they passing over it. One parameter says so; two booleans invited a
// call site to get one right and the other wrong.
enum class RowActivation { Click, Cursor };

// What a press on a sidebar row means: which of the things drawn on it the
// pointer was actually on, and what that one does.
//
// Beside `activateSidebarRow` because it is the outer half of the same
// question. A row is not one target -- a note row carries a disclosure
// triangle, an icon, a name and a dot per tag, and each is a different answer
// -- and the order they are tested in *is* the design: the innermost control
// wins, or the row underneath swallows it. Spelled out in the shell's key
// handler, that order was invisible next to a dozen unrelated branches.
void pressSidebarRow(UiRuntime& ui, const SidebarRow& row, float x, float y, Uint8 button);

// Walking the list with the keyboard, and the other half of the same question.
//
// `moveTreeCursor` steps the cursor by `delta` and opens whatever it lands on;
// `expandTreeCursor` is Right and Left on the row it is sitting on -- open a
// folder or a band, or step to the neighbour when there is nothing to open.
// `revealSidebarRow` scrolls a row into view from last frame's geometry, which
// is all that is needed to know whether it is off an edge and by how much.
//
// Here with `pressSidebarRow` rather than in the shell's key handler because
// the two are the same behaviour reached two ways, and the rules they share are
// not obvious: a caption is stepped over and a band is stopped on, a folder
// opens without becoming the selection, and a note passed over takes the tab
// showing rather than opening one of its own. Split between two files, the
// pointer and the keyboard drifted -- which is how the collapsing came to be
// reachable only with a mouse.
void moveTreeCursor(UiRuntime& ui, int delta);
void expandTreeCursor(UiRuntime& ui, bool open);
void revealSidebarRow(UiRuntime& ui, std::size_t index);

// Enter on the row the cursor is sitting on, and where focus should go next.
//
// A walk *passes over* the rows it can open -- a note takes the tab showing, a
// folder is not unfolded -- and deliberately does not act on the two kinds it
// cannot: a tag replaces the entire row list, and a band shuts it, so either
// one applied by an arrow key would destroy the list being walked. So those two
// need a key that means "this one, now", and Enter is it.
//
// Returns the focus the caller should take: the page for a note or a folder,
// because that is what was opened, and the list for a tag or a band, because
// the list is what changed and the reader is still in it.
FocusArea chooseSidebarCursorRow(UiRuntime& ui);

// The one place a sidebar row turns into a selection, so a click, an arrow key
// and a drop can never disagree about what selecting a row means.
//
// Here rather than in Application.cpp, where it was `static` and therefore
// untestable, because it is the sidebar's model half: it reads a row and moves
// the selection, and touches no renderer.
void activateSidebarRow(UiRuntime& ui, const SidebarRow& row, RowActivation how);

}
