#pragma once

#include <SDL3/SDL_pixels.h>

#include <string_view>

namespace micronotes::ui {

enum class ThemeMode {
  Light,
  Dark
};

// Semantic color roles. Every color drawn by the application comes from here so
// a palette swap is a single assignment rather than a search across draw code.
struct Theme {
  ThemeMode mode = ThemeMode::Dark;

  // Text
  SDL_Color text;            // primary body and heading text
  SDL_Color muted;           // secondary text: unselected rows, captions
  SDL_Color dim;             // tertiary text: section labels, counts, markers
  SDL_Color onAccent;        // text drawn on top of an accent fill

  // Accent
  SDL_Color accent;          // links, caret, active affordances
  // A link to a note that is not there yet. Not an error state: writing the
  // link before the note is the ordinary way round, so this reads as an offer
  // rather than as a warning.
  SDL_Color linkPending;
  SDL_Color accentDim;       // accent borders and scrollbar thumbs
  SDL_Color accentSoft;      // accent-tinted fills behind chips and callouts
  SDL_Color warn;            // destructive actions, unsaved indicator

  // Surfaces
  SDL_Color appBg;
  SDL_Color sidebarBg;
  SDL_Color notesBg;
  SDL_Color editorBg;
  SDL_Color viewerBg;
  SDL_Color statusBg;
  SDL_Color pageSurface;     // the editing/reading page itself
  SDL_Color surface;         // generic raised panel
  SDL_Color surfaceElevated; // menus and popovers
  SDL_Color surfaceSheen;    // 1px top highlight on a raised surface
  SDL_Color inputBg;
  SDL_Color codeBg;
  SDL_Color calloutBg;
  SDL_Color chipBg;
  SDL_Color tableHeaderBg;
  SDL_Color tableCellBg;

  // Lines
  SDL_Color divider;         // strong separator
  SDL_Color hairline;        // subtle separator and default border

  // States
  SDL_Color hoverBg;
  SDL_Color selectedBg;
  SDL_Color selectionBg;     // text selection highlight
  SDL_Color findBg;          // find-in-note match fill
  SDL_Color findBorder;

  // Scrollbars
  SDL_Color scrollTrack;
  SDL_Color scrollThumb;
  SDL_Color scrollThumbBorder;
};

// A callout's identity is its `[!KIND]` tag. The accent draws the rule and the
// label; the surface is that accent laid over the page, so the tint stays
// legible in either palette without a token per kind.
struct CalloutStyle {
  SDL_Color accent;
  SDL_Color surface;
};

CalloutStyle calloutStyle(std::string_view kind);

const Theme& theme();
ThemeMode themeMode();
void setThemeMode(ThemeMode mode);

// Round-trips through the persisted UI state file.
std::string_view themeModeName(ThemeMode mode);
ThemeMode themeModeFromName(std::string_view name);

}
