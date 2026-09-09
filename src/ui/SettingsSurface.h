#pragma once

#include "CoreAliases.h"

#include "core/editor/TextField.h"
#include "ui/Rect.h"
#include "ui/ScrollList.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The Settings surface: a modal card with a filter across the top, a rail of
// categories down the left, the settings themselves on the right, and a count
// along the foot. The same card, in its other mode, is About.
//
// It replaces a five-row list that drilled into a sub-list per setting -- two
// trips through an overlay to change two things, and a surface with no room to
// say what any setting *did*. Every row here carries its own control and its own
// one-line description, so the value and the reason for it are read together.
//
// Split from the paint for the reason the rest of `ui/` is: what the rows are,
// what the filter matches, which row the keyboard is on and where the panes sit
// are all decidable without a renderer, and so are testable. What a row's
// current value *is* comes from `app/SettingsPane.cpp`, which can see the
// workspace and the theme.
namespace micronotes::ui {

enum class SettingsMode {
  Settings,
  // Every command with the keys it answers to, plus what the app is and which
  // version of it this is. The same card, because it is the same question asked
  // one step further out: "what can this thing do, and what is it?"
  About
};

// What a row is changed with. Each is a shape rather than a widget library: the
// kind decides which boxes the row reserves on its right, and clicking one of
// those boxes is what `settingsRowActivated` reports.
enum class SettingControl {
  // A row that only reads. About's rows, and a setting the app shows but does
  // not let you change here.
  None,
  Checkbox,
  // One value out of a short list, cycled by clicking the chip. Right for two
  // or three choices where the choices have no order.
  Segmented,
  // The same, with a previous and a next: right where the choices *do* have an
  // order, so "bigger" and "smaller" are one click each rather than a cycle
  // you have to go the long way round.
  Stepper,
  // A row that opens something else -- the library folder, which is a path and
  // cannot be cycled.
  Action
};

struct SettingsRow {
  // Stable, and what an activation reports back. Not the label: a label is
  // allowed to be reworded.
  std::string id;
  std::string category;
  std::string label;
  // Why the setting exists, in one line. The old list had nowhere to put this,
  // so "Page width: Medium" had to be self-explanatory and was not.
  std::string description;
  SettingControl control = SettingControl::None;
  // Checkbox rows.
  bool checked = false;
  // Everything else: what it is set to, already spelled for reading.
  std::string value;
  // Whether it has been moved off the value a fresh library starts with, and so
  // whether the row offers to put it back.
  bool resettable = false;
};

struct AboutRow {
  std::string label;
  std::string detail;
};

// Which pane has the keyboard. Three, because all three take keystrokes: the
// filter takes text, the rail takes up and down, and so do the values.
enum class SettingsPaneFocus {
  Filter,
  Categories,
  Values
};

// Where a settings row's control and its reset button sit, inside the row.
// Recorded rather than derived at the click, because the click has to land on
// the same boxes the paint drew.
struct SettingsRowBoxes {
  Rect row;
  Rect reset;
  Rect checkbox;
  Rect value;
  Rect previous;
  Rect next;
};

// What the surface is showing and where the keyboard is. Held on the runtime,
// because a modal that forgets its filter every frame is a modal you cannot type
// into.
struct SettingsSurfaceState {
  bool visible = false;
  SettingsMode mode = SettingsMode::Settings;
  editor::TextField query;
  SettingsPaneFocus focus = SettingsPaneFocus::Values;
  // Index into the category list, and into the rows of that category.
  int category = 0;
  int row = 0;
  // How far each of the card's two lists is scrolled, and how many rows the
  // last paint fitted into it. One `RowStrip` per list rather than an offset
  // and a count per list spelled out as four ints: the two travel together --
  // every clamp reads both -- and as separate fields they were clamped by hand
  // at nine sites, twice each.
  RowStrip rows;
  RowStrip about;

  // What the last frame drew, so a click finds what it landed on without laying
  // the surface out twice.
  Rect panel;
  Rect filter;
  Rect rail;
  Rect values;
  std::vector<Rect> categoryRects;
  std::vector<SettingsRowBoxes> rowBoxes;
};

// Where the surface's regions go inside a window of this size.
//
// Pure arithmetic over the window: the card is centred and bounded, and the
// bands across its head and foot are fixed. What is *in* the panes depends on
// the rows and on the font, so it is not decided here.
struct SettingsLayout {
  Rect panel;
  Rect header;
  Rect filter;
  Rect rail;          // the category list
  Rect sectionHeader; // the selected category's title and subtitle
  Rect values;        // the rows themselves
  Rect footer;

  friend bool operator==(const SettingsLayout&, const SettingsLayout&) = default;
};

SettingsLayout settingsLayout(float windowWidth, float windowHeight, SettingsMode mode);

// The card's proportions, and the bands inside it.
inline constexpr float kSettingsWidthFraction = 0.72f;
inline constexpr float kSettingsHeightFraction = 0.74f;
inline constexpr float kSettingsMinWidth = 520.0f;
inline constexpr float kSettingsMaxWidth = 900.0f;
inline constexpr float kSettingsMinHeight = 320.0f;
inline constexpr float kSettingsFilterHeight = 30.0f;
inline constexpr float kSettingsFooterHeight = 26.0f;
inline constexpr float kSettingsRailWidth = 168.0f;
inline constexpr float kSettingsSectionHeaderHeight = 46.0f;
inline constexpr float kSettingsCategoryRowHeight = 28.0f;

// The categories the rows fall into, in the order they were first seen. Order is
// the order of the rows, so a category is placed by where its settings are
// rather than by a second list that could disagree with them.
std::vector<std::string> settingsCategories(const std::vector<SettingsRow>& rows);

// The rows of one category that survive the filter, as indices into `rows`.
//
// An empty query matches everything. A query is matched against the label, the
// category and the description -- the description included on purpose: somebody
// looking for "how wide" will not type "page width".
std::vector<int> settingsRowsIn(const std::vector<SettingsRow>& rows, std::string_view category,
                                std::string_view query);

// Whether any row of `category` survives the filter. Categories that match
// nothing are still listed but cannot be selected, so the rail does not jump
// around as you type.
bool settingsCategoryMatches(const std::vector<SettingsRow>& rows, std::string_view category,
                             std::string_view query);

// The category the selection names, and the rows visible under it.
//
// One function because it was six copies. "Which rows can the keyboard move
// through" is four steps -- list the categories, clamp the selected one into
// that list, filter its rows, clamp the selected row into what survived -- and
// the click, the keys, the wheel, the paint and the clamp each wrote all four
// out. A seventh copy had already been written as a `selectedRow` helper and
// then not called by any of them, which is how a duplicated sequence announces
// itself: the shared version exists and nobody reaches it.
//
// `category` and `row` are what the surface currently holds, which may be past
// the end after a filter has emptied a category; the returned pair is inside
// the lists or names nothing.
struct SettingsSelection {
  // `category` clamped into the category list, and the index into `visible`.
  // Both zero when there is nothing to select.
  int category = 0;
  int row = 0;
  // Rows of that category that survive the filter, as indices into `rows`.
  std::vector<int> visible;

  bool empty() const { return visible.empty(); }
  // The row the selection names, as an index into `rows`, or -1.
  int selected() const { return visible.empty() ? -1 : visible[static_cast<std::size_t>(row)]; }
};

SettingsSelection settingsSelection(const std::vector<SettingsRow>& rows, int category, int row,
                                    std::string_view query);

// Every row that survives the filter, across all categories, for the count
// along the foot.
std::size_t settingsMatchCount(const std::vector<SettingsRow>& rows, std::string_view query);

// The About rows that survive the filter.
std::vector<int> aboutRowsMatching(const std::vector<AboutRow>& rows, std::string_view query);

}
