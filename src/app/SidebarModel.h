#pragma once

#include "app/Shell.h"
#include "ui/Rect.h"

#include <cstddef>
#include <optional>

// The sidebar's row list: what rows exist, where each one sits, and which one
// is under the pointer. Split out of Application.cpp because it is a model
// rather than a paint -- it reads the library and produces geometry, touches no
// renderer, and is the one part of the sidebar that has a cache to get wrong.
//
// The list is O(library) to build and O(viewport) to draw: the tree relativises
// a path and builds a map key for every note in the library before it can place
// the first row. It used to be rebuilt on every frame, which on a 400-note
// library was most of the frame. buildSidebarRows() is the memo in front of it.
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

// The results the sidebar is listing, recomputed only when the question or the
// library has changed. Each query is a hit on SQLite.
const std::vector<library::SearchResult>& searchResults(UiRuntime& ui);

// Fills `ui.sidebarRows` for a list occupying `rect`, reusing the standing list
// when nothing it depends on has moved. An empty result means the list holds
// nothing worth drawing -- the caller draws its empty message instead, and
// hit-testing finds nothing, which is the same answer.
void buildSidebarRows(UiRuntime& ui, ui::Rect rect);

// The row under the pointer, or nothing when the pointer is off the list.
std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, ui::Rect sidebar, float x, float y);

}
