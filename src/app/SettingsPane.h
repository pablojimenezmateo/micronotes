#pragma once

#include "ui/TextRenderer.h"
#include "ui/Rect.h"
#include "ui/SettingsSurface.h"

#include <SDL3/SDL.h>

#include <vector>

namespace micronotes::app {

struct UiRuntime;

using micronotes::ui::Rect;

// The Settings surface, and About in the same card.
//
// Two sources behind this one header, because the file was doing three jobs at
// 818 lines: `SettingsPane.cpp` builds the rows and draws them,
// `SettingsInput.cpp` answers a press and a keystroke on them. The two share no
// helper -- the paint's are all about measuring text into a column, the input's
// all about stepping a value -- so the seam was already there.
//
// It replaces the five-row overlay that drilled into a list per setting. That
// shape cost two trips through a modal to change two things and had nowhere to
// say what any setting did, so "Page width: Medium" had to explain itself and
// could not. Here every row carries its own control and its own line of help,
// the filter reaches the help as well as the label, and the rail groups the
// settings so the card is readable before it is searched.
//
// The rows are built from live state on every frame the surface is open. There
// are seven of them and each is a read of a setting or a bool on the workspace;
// caching them would mean invalidating the cache at every site that changes a
// setting, and the site that forgot would leave the card describing a value the
// app no longer holds.

// A row the card cannot answer by itself asks the shell to. There are two: the
// library folder is a path, which wants the text prompt that already exists for
// it, and the shortcut list is its own surface.
//
// Returned rather than run, because both of those live with the rest of the
// dispatch in Application.cpp and this file has no business reaching into it --
// the same rule the icon rail follows.
enum class SettingsRequest {
  None,
  LibraryFolder,
  Shortcuts
};

struct SettingsOutcome {
  // The surface is modal, so this is true for every event while it is open: a
  // press outside the card closes it rather than reaching the note behind.
  bool handled = false;
  SettingsRequest request = SettingsRequest::None;
};

// Carries out whatever a row asked the shell for. One function rather than the
// same two-branch switch at the press router and the key router, which is where
// the two would drift.
void carryOutSettingsRequest(UiRuntime& ui, SettingsRequest request);

void openSettingsSurface(UiRuntime& ui);
void openAboutSurface(UiRuntime& ui);
void closeSettingsSurface(UiRuntime& ui);

// The rows the surface lists, and the About rows in its other mode. Exposed
// because they are the content of the surface and more than the paint needs to
// know it: a test that every row can actually be operated reads these rather
// than a second copy of them.
std::vector<ui::SettingsRow> settingsRows(const UiRuntime& ui);
std::vector<ui::AboutRow> aboutRows();

void drawSettingsSurface(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                         int windowWidth, int windowHeight);

SettingsOutcome handleSettingsClick(UiRuntime& ui, float x, float y);
SettingsOutcome handleSettingsKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift);
bool handleSettingsText(UiRuntime& ui, const char* input);
bool handleSettingsWheel(UiRuntime& ui, float dy);

}
