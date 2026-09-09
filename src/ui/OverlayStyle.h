#pragma once

#include "ui/Menus.h"
#include "ui/Metrics.h"
#include "ui/Overlay.h"
#include "ui/TextRenderer.h"

// The numbers and the faces an overlay is measured and painted with.
//
// Shared by `Overlay.cpp`, which lays one out, and `OverlayPaint.cpp`, which
// draws it -- the same split `PageView.cpp` and `PageViewPaint.cpp` already
// have, and for the same reason: the geometry is one function that the paint
// and the hit test both read, so it must not be one that either of them owns.
//
// Private to those two. It is a header only because a translation unit boundary
// runs between them.
namespace micronotes::ui {

// A row, and the field over it. The row is the menu popup's row: the palette,
// the context menus and the menu bar's popups are the same object -- a list of
// commands with their accelerators -- and they were three heights, so a context
// menu and the palette that can run the same command looked unrelated.
constexpr float kRowHeight = kMenuPopupItemHeight;
constexpr float kFieldHeight = 26.0f;
constexpr float kPadding = 10.0f;
// Inside a row. Written out as 10 and 20 at seven sites; the gap between a
// row's two trailing pieces went with them into `ui::drawMenuRow`, which is now
// the one place a popup row is painted.
constexpr float kRowPadX = kPadding;
// A Confirm's two buttons. Equal width, because they are two answers to one
// question and the wider of two buttons reads as the recommended one -- which
// on a deletion is the wrong recommendation to make by accident.
constexpr float kButtonWidth = 96.0f;
// A colour picker's grid. Six across so twelve swatches are two rows -- both in
// the eye at once, which is the whole reason it is a grid and not a list -- and
// a cell big enough to be a colour rather than a pixel.
constexpr int kSwatchColumns = 6;
constexpr float kSwatchCell = 30.0f;
constexpr float kSwatchGap = 4.0f;
// The header band a titled overlay wears -- see `drawTitledCard`. The menu
// bar's height, because it is the same kind of surface and two chrome strips
// that differ by three pixels read as a mistake.

// A row can be landed on when it is a real row that is not disabled. Section
// headings are listed as disabled rows and separators are not rows at all, so
// both are drawn and both are skipped -- which is the whole reason the
// distinction is a field rather than an empty label.
inline bool selectable(const OverlayItem& item) {
  return item.enabled && !item.separator;
}

// The menu bar's own popups measure their rows with `menuRowHeight` too, so a
// context menu and a menu-bar menu are one shape rather than two that happen to
// use the same numbers.
inline float rowHeight(const OverlayItem& item) {
  return menuRowHeight(item.separator);
}

// The three faces an overlay sets, named once so the layout reserves room in
// the same style the draw paints in. They had drifted: the hint's height was
// reserved in Sans at the `tiny` size and drawn in Mono at 0.85 of `chrome`,
// and the title's in Sans/`small`-bold and drawn in Mono/`chrome`-bold. Both
// pairs happen to be within a pixel, which is exactly why nobody noticed.
inline TextStyle titleFace() {
  return {FontFamily::Mono, true, false, type().chrome};
}
inline TextStyle hintFace() {
  return chromeSmallStyle();
}
}
