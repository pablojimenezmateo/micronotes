#pragma once

#include "app/Shell.h"
#include "ui/Rect.h"

#include <cstddef>
#include <functional>
#include <optional>
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
inline constexpr float kSidebarRowHeight = 26.0f;
inline constexpr float kSidebarTagHeight = 24.0f;
inline constexpr float kSidebarLabelHeight = 34.0f;
inline constexpr float kSidebarIndent = 13.0f;
inline constexpr float kSidebarResultTitleHeight = 24.0f;
inline constexpr float kSidebarSnippetHeight = 16.0f;

float searchResultRowHeight(std::size_t matchLines);

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
void buildSidebarRows(UiRuntime& ui, ui::Rect rect);

// Trims row `index`'s matching lines to the column, once. Called by the draw for
// the rows it is about to paint rather than by the build for all of them:
// trimming one line measures it and then bisects with a measurement per probe,
// and every probe is a string nothing has measured before, so it costs about
// 0.25 ms and the measure cache cannot help. A 200-result query carries 600 of
// them -- 150 ms and up in the first frame after a keystroke in the search box,
// to produce the three dozen lines a sidebar can show.
//
// The row keeps its own trimmed lines, so scrolling back over a row does not
// trim it again, and the row list still holds every result because the heights
// have to add up to a scrollbar.
void fillSearchSnippets(UiRuntime& ui, std::size_t index, float width,
                        const SnippetMeasure& measure);

// The row under the pointer, or nothing when the pointer is off the list.
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, ui::Rect sidebar, float x, float y);

}
