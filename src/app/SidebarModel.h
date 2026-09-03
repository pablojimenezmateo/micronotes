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
inline constexpr float kSidebarIndent = 13.0f;

// Left to right inside a row: the gutter that holds a disclosure triangle or a
// note's icon, then the label.
//
// One pair of numbers for the whole list. A section heading, a tag, a search
// result and a tree row are four kinds of row in one column, and they used to
// start their text at four different x -- 18, 20, 20 and 28 -- because each was
// nudged into place on its own. Nothing in the list lined up with anything else.
inline constexpr float kSidebarGutterX = ui::kSpace1;
inline constexpr float kSidebarGutterWidth = 16.0f;
inline constexpr float kSidebarLabelX = kSidebarGutterX + kSidebarGutterWidth + ui::kSpace1;

// The sidebar's vertical rhythm.
//
// Every row holds a line of text, and text grows with the reader's text size
// while chrome stays put -- so a row nailed to 26 pixels was a row that, at the
// large size, drew its label into the row beneath it. The floors are what the
// medium size already gave, so a reader who has not touched the setting sees
// nothing move.
//
// Taken as line heights rather than as a renderer, so the model stays free of
// the font: the draw measures, this decides, and both are testable apart.
struct SidebarMetrics {
  float row = 26.0f;
  float tag = 24.0f;
  float label = 34.0f;
  float resultTitle = 24.0f;
  float snippet = 16.0f;
};

SidebarMetrics sidebarMetrics(int uiLineHeight, int snippetLineHeight);

float searchResultRowHeight(std::size_t matchLines, const SidebarMetrics& metrics);

// The width of a snippet line, in the font the rows draw it in.
using SnippetMeasure = std::function<int(std::string_view)>;

// The results the sidebar is listing, recomputed only when the question or the
// library has changed. Each query is a hit on SQLite.
const std::vector<library::SearchResult>& searchResults(UiRuntime& ui);

// Fills `ui.sidebarRows` for a list occupying `rect`, reusing the standing list
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
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, ui::Rect sidebar, float x, float y);

}
