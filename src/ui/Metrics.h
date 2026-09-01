#pragma once

namespace micronotes::ui {

// Every fixed size the shell lays itself out with, in logical pixels.
//
// One definition per number, because the same size is read from three places
// that must agree: what a fresh library gets, what a session restore falls back
// to when the persisted value is unusable, and what double-clicking a divider
// returns to. Spelling a width out at each of those sites is how they drift.
//
// Sizes that should grow with the reader's text size are not here -- those
// belong to ui::TypeScale, which ui::textScale() multiplies. Chrome stays put
// when the type grows, so chrome is measured here.

// Horizontal strips across the content column, top to bottom.
inline constexpr float kTabStripHeight = 34.0f;
inline constexpr float kBreadcrumbHeight = 30.0f;
inline constexpr float kStatusBarHeight = 28.0f;

// Panel widths a fresh library starts with.
inline constexpr float kDefaultSidebarWidth = 240.0f;
inline constexpr float kDefaultNoteListWidth = 300.0f;
inline constexpr float kDefaultRightPanelWidth = 280.0f;

// The narrowest each panel may be dragged to, and the narrowest the page may be
// squeezed to before the panels start giving room back instead.
inline constexpr float kMinSidebarWidth = 170.0f;
inline constexpr float kMinNoteListWidth = 220.0f;
inline constexpr float kMinRightPanelWidth = 200.0f;
inline constexpr float kMinContentWidth = 320.0f;

// Squeeze floors. A window too narrow to honour the minimums above has to put
// the difference somewhere, and a panel thinner than this is not worth drawing.
inline constexpr float kSidebarSqueezeFloor = 150.0f;
inline constexpr float kNoteListSqueezeFloor = 190.0f;
inline constexpr float kRightPanelSqueezeFloor = 170.0f;

// No panel may take more than its share of the window, however wide it was
// dragged on a larger screen and then persisted.
inline constexpr float kMaxSidebarFraction = 0.28f;
inline constexpr float kMaxNoteListFraction = 0.34f;
inline constexpr float kMaxRightPanelFraction = 0.30f;

// Layout treats a window narrower than this as if it were this wide, so the
// panel arithmetic below cannot produce negative rects on a tiny window.
inline constexpr float kMinUsableWidth = 760.0f;

// Below this the side panels drop to their compact widths rather than eating
// the page. The hysteresis band keeps a slow drag across the boundary from
// flipping back and forth on every motion event.
inline constexpr float kCompactBreakpoint = 1000.0f;
inline constexpr float kCompactHysteresis = 12.0f;
inline constexpr float kCompactSidebarWidth = 190.0f;
inline constexpr float kCompactNoteListWidth = 240.0f;

// Interaction affordances. The grab region and the region that changes the
// cursor are the same region, so one number governs both and they cannot drift.
inline constexpr float kResizeGutterInflate = 3.0f;
inline constexpr float kScrollbarHitInflate = 4.0f;

}
