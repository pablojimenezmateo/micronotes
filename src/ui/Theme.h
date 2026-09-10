#pragma once

#include <SDL3/SDL_pixels.h>

#include <string>
#include <string_view>

namespace micronotes::ui {

enum class ThemeMode {
  Light,
  Dark
};

// Semantic color roles. Every color drawn by the application comes from here so
// a palette swap is a single assignment rather than a search across draw code.
//
// The vocabulary is an IDE's rather than a document editor's: the window is
// divided into *chrome* (the menu bar, the tab strips, the status bar), *panels*
// (the sidebar, the right-hand panel, popups) and the *editor* (the note). Each
// of the three has its own ground and its own text, and the separation between
// them is what makes a flat interface -- one with no shadows and no radii --
// legible at all. The roles and their values come from the sibling microide
// tree, `src/render/Theme.h`; the ones microide has no counterpart for (the
// pending-link colour, the code and table grounds) are micronotes' own and are
// derived off the same grounds so a palette change carries them along.
struct Theme {
  ThemeMode mode = ThemeMode::Dark;

  // Chrome: the strips along the edges of the window.
  SDL_Color windowBackground;    // behind everything, and the gaps between panes
  SDL_Color chromeBackground;    // menu bar, tab strip, breadcrumb, status bar
  SDL_Color chromeActive;        // the active tab, the open menu
  SDL_Color chromeText;
  SDL_Color chromeActiveText;
  SDL_Color chromeTextSecondary;

  // Panels: the sidebar, the right-hand panel, and what floats over them.
  SDL_Color surfaceBackground;   // a panel's ground, and a text field's
  SDL_Color surfaceRaised;       // a control on a panel: chips, toggles, tracks
  SDL_Color surfaceText;
  SDL_Color overlayBackground;   // menus, popovers, the command palette
  SDL_Color overlayBackdrop;     // the dim over the window behind a palette

  // The note itself.
  SDL_Color editorBackground;
  SDL_Color gutterBackground;    // the strip of page left of the text column
  SDL_Color codeBackground;      // fenced blocks and inline code
  SDL_Color tableHeaderBackground;

  // Text, in four steps. Body prose down to the markers nobody reads word by
  // word; `textDisabled` is a control that will not answer.
  SDL_Color textPrimary;
  SDL_Color textSecondary;
  SDL_Color textMuted;
  SDL_Color textDisabled;
  SDL_Color onAccent;            // text drawn on top of an accent fill

  // Accent and alarm.
  SDL_Color accent;              // links, caret, active affordances
  // A link to a note that is not there yet. Not an error state: writing the
  // link before the note is the ordinary way round, so this reads as an offer
  // rather than as a warning.
  SDL_Color linkPending;
  SDL_Color warn;                // destructive actions, unsaved indicator
  SDL_Color cursor;

  // One line weight. A flat interface that draws both a "strong" and a "subtle"
  // separator ends up with two grids that do not line up; microide draws one.
  SDL_Color border;

  // States.
  //
  // Hover and selection share a ground: what distinguishes the selected row is
  // the 2px accent strip `drawRow` puts down its leading edge, not a third
  // shade nobody can tell from the second.
  SDL_Color rowHighlight;
  SDL_Color selectionFill;       // text selection, translucent
  SDL_Color selectionStrong;     // the same, composited, for solid panels
  SDL_Color searchMatch;         // find-in-note and sidebar snippet matches
  SDL_Color searchMatchActive;   // the match the cursor is on
};

// A callout's identity is its `[!KIND]` tag. The accent draws the rule and the
// label; the surface is that accent laid over the page, so the tint stays
// legible in either palette without a token per kind.
struct CalloutStyle {
  SDL_Color accent;
  SDL_Color surface;
};

CalloutStyle calloutStyle(std::string_view kind);
// The same, for a palette that is not the one the window is currently in.
// The PDF exporter is the caller: a note printed in the dark theme is a page
// of white text on a black rectangle, which is a waste of ink and reads as a
// bug rather than as a preference, so an export is always composed against
// the light palette whatever the app is showing.
CalloutStyle calloutStyle(std::string_view kind, ThemeMode mode);

// A callout's name as it is drawn: the kind in title case, and "Note" when the
// author named no kind at all.
//
// Normalised rather than echoed, for the reason `calloutStyle` normalises its
// input: the `[!KIND]` tag is read as the author wrote it, so anything that
// echoes it draws `[!note]` and `[!NOTE]` under two different names.
std::string calloutLabel(std::string_view kind);

const Theme& theme();
// A named palette rather than the active one. Same caller, same reason.
const Theme& themeFor(ThemeMode mode);
ThemeMode themeMode();
void setThemeMode(ThemeMode mode);

// Round-trips through the persisted UI state file.
std::string_view themeModeName(ThemeMode mode);
ThemeMode themeModeFromName(std::string_view name);

}
