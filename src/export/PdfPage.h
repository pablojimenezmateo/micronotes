#pragma once

#include "ui/Theme.h"

#include <SDL3/SDL_pixels.h>

// The shape of a page and the ink on it.
//
// Everything here is in **points** -- 72 to the inch, which is the only unit a
// PDF has. Nothing in this layer is in pixels, and the one conversion from the
// screen's sizes to these is `kPrintScale` below, applied once where the type
// scale is built.
namespace micronotes::exporting {

// A4, which is one page size and not a setting. A Letter option would be one
// more thing to persist, one more row in a settings pane, and a second set of
// margins to keep in proportion; the difference between the two is 18mm of
// height, and a page of notes laid out for A4 prints on Letter with a wider
// bottom margin and nothing clipped.
inline constexpr float kPageWidth = 595.28f;
inline constexpr float kPageHeight = 841.89f;

// About 20mm all round. The content column that leaves is 483 points, which at
// the body size below is roughly the measure the reading pane uses on screen:
// 44 characters of the body face per line, against the reading column's 45.
inline constexpr float kMarginX = 56.0f;
inline constexpr float kMarginTop = 56.0f;
// Deeper than the top, because the page number sits in it.
inline constexpr float kMarginBottom = 64.0f;

inline constexpr float kContentWidth = kPageWidth - kMarginX * 2.0f;
inline constexpr float kContentHeight = kPageHeight - kMarginTop - kMarginBottom;

// Screen sizes are logical pixels and page sizes are points, and this is the
// whole of the conversion between them: an 16px body becomes an 11pt one.
// Applied once, where the type scale is built, so no other number in this
// layer has to remember which unit it is in.
inline constexpr float kPrintScale = 11.0f / 16.0f;

// The running furniture, in the same small size at both ends of the page.
inline constexpr float kFurnitureSize = 8.0f;
// Where the page number's baseline sits, measured from the top of the page.
inline constexpr float kFooterBaseline = kPageHeight - kMarginBottom + 30.0f;
// And the running note title, above the content rather than in the top margin
// proper, with a hairline under it.
inline constexpr float kHeaderBaseline = kMarginTop - 16.0f;

// The gap between one note and the next when several are exported into one
// file. Notes never share a page: a folder's PDF is its notes bound together,
// not its notes concatenated.
inline constexpr float kTitleBlockGap = 22.0f;

// Always the light palette, never the one the window is in. See
// `ui::calloutStyle(kind, mode)` for the argument.
inline const ui::Theme& ink() {
  return ui::themeFor(ui::ThemeMode::Light);
}

inline ui::CalloutStyle calloutInk(std::string_view kind) {
  return ui::calloutStyle(kind, ui::ThemeMode::Light);
}

// White, rather than the light theme's own page colour.
//
// The light theme's editor background is very slightly off-white, which on a
// screen is the difference between a page and the pane behind it. On paper
// there is no pane behind it, and an off-white fill is a rectangle of ink
// covering every sheet that goes through a printer.
inline constexpr SDL_Color kPaper {255, 255, 255, 255};

}
