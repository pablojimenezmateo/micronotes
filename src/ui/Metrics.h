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

// The strip along the very top of the window, full width above the panels: the
// breadcrumb, the favourite star, and the window controls this borderless
// window draws for itself. Full width rather than over the page alone, because
// the close button has to sit in the actual corner of the window -- a control
// inset from the corner is one the pointer cannot be thrown at.
inline constexpr float kTitleBarHeight = 30.0f;

// Horizontal strips across the content column, top to bottom.
inline constexpr float kTabStripHeight = 34.0f;
inline constexpr float kStatusBarHeight = 28.0f;

// The drawn window controls: minimise, maximise, close, laid out right to left
// so close ends at the corner.
inline constexpr float kWindowButtonWidth = 40.0f;

// How wide the invisible resize border around a borderless window is. The hit
// test and nothing else reads this, but it lives here because it is a chrome
// size like the rest.
inline constexpr float kWindowFrameThickness = 6.0f;

// Corner radii. Three steps, because a shell with a radius per control is a
// shell where nothing lines up: `small` is for controls the size of a row --
// chips, checkboxes, the search field; `medium` is for blocks inside the page
// -- callouts, code, properties; `large` is for things that float over it.
// The mark in a callout's gutter. Both surfaces are the same renderer now, so
// this has one reader -- but it stays named, because `7.0f` and `+ 6.0f` written
// out inline is how it came to be two shapes in two files in the first place.
inline constexpr float kCalloutMarkSize = 7.0f;
inline constexpr float kCalloutMarkInset = 6.0f;

inline constexpr float kRadiusSmall = 4.0f;
inline constexpr float kRadiusMedium = 8.0f;
inline constexpr float kRadiusLarge = 12.0f;

// The icon rail down the leading edge of the window. Fixed: it holds icons of
// one size and nothing that could want more room, so it is not resizable and
// has no persisted width.
inline constexpr float kRibbonWidth = 44.0f;
inline constexpr float kRibbonButtonSize = 32.0f;
inline constexpr float kRibbonIconSize = 16.0f;
// Between two buttons, and above the first and below the last. Read by the
// placement and by nothing else, but they belong beside the button size: how
// many controls a short column can hold is arithmetic over all three.
inline constexpr float kRibbonGap = 4.0f;
inline constexpr float kRibbonEdgePad = 6.0f;

// The band a callout reserves above its first line for the kind's icon and
// name. Layout reserves it and the draw fills it, so the number is here rather
// than in either of them.
inline constexpr float kCalloutTitleHeight = 24.0f;

// Panel widths a fresh library starts with. The sidebar is wider than a plain
// tree needs because it also holds the search field and its results.
inline constexpr float kDefaultSidebarWidth = 280.0f;
inline constexpr float kDefaultRightPanelWidth = 280.0f;

// The search field at the top of the sidebar, and the strip it sits in.
inline constexpr float kSidebarSearchHeight = 34.0f;
inline constexpr float kSidebarSearchBand = 58.0f;

// The narrowest each panel may be dragged to, and the narrowest the page may be
// squeezed to before the panels start giving room back instead.
inline constexpr float kMinSidebarWidth = 200.0f;
inline constexpr float kMinRightPanelWidth = 200.0f;
inline constexpr float kMinContentWidth = 320.0f;

// Squeeze floors. A window too narrow to honour the minimums above has to put
// the difference somewhere, and a panel thinner than this is not worth drawing.
inline constexpr float kSidebarSqueezeFloor = 180.0f;
inline constexpr float kRightPanelSqueezeFloor = 170.0f;

// No panel may take more than its share of the window, however wide it was
// dragged on a larger screen and then persisted.
inline constexpr float kMaxSidebarFraction = 0.34f;
inline constexpr float kMaxRightPanelFraction = 0.30f;

// Layout treats a window narrower than this as if it were this wide, so the
// panel arithmetic below cannot produce negative rects on a tiny window.
inline constexpr float kMinUsableWidth = 760.0f;

// Below this the side panels drop to their compact widths rather than eating
// the page. The hysteresis band keeps a slow drag across the boundary from
// flipping back and forth on every motion event.
inline constexpr float kCompactBreakpoint = 1000.0f;
inline constexpr float kCompactHysteresis = 12.0f;
inline constexpr float kCompactSidebarWidth = 220.0f;

// The accent strip down the edge of the pane that has the keyboard. A selected
// row used to have one too, at a different width so the two could not be
// confused; it is a rounded fill now, and the pane edge is the only strip left.
inline constexpr float kFocusEdgeWidth = 2.0f;

// The nesting guides down the sidebar's tree: one hairline per level of depth,
// so a note six folders down is visibly under the folder it belongs to rather
// than merely further right than its neighbours.
inline constexpr float kTreeGuideWidth = 1.0f;

// The spacing scale. Every gap and inset in the shell is one of these, so a
// row in the sidebar and a row in a panel are inset by the same amount rather
// than by whichever number was to hand -- which is how "rect.x + 14" here and
// "rect.x + 12" there stop being a decision anybody made.
inline constexpr float kSpace1 = 4.0f;
inline constexpr float kSpace2 = 8.0f;
inline constexpr float kSpace3 = 12.0f;
inline constexpr float kSpace4 = 16.0f;
inline constexpr float kSpace6 = 24.0f;

// A tooltip sits this far off the control it describes.
inline constexpr float kTooltipGap = 6.0f;
inline constexpr float kTooltipPadX = 10.0f;
inline constexpr float kTooltipPadY = 5.0f;

// Interaction affordances. The grab region and the region that changes the
// cursor are the same region, so one number governs both and they cannot drift.
inline constexpr float kResizeGutterInflate = 3.0f;
inline constexpr float kScrollbarHitInflate = 4.0f;

// The vertical scrollbar every scrolling surface draws: the track's inset from
// the viewport's trailing edge and from its ends, its width, and the thumb --
// wider than the track, so it reads as a handle on it rather than as a fill of
// it.
//
// Here because the numbers were written out four times: twice inside
// `ui::drawVerticalScrollbar` and its `scrollbarTrack`/`scrollbarThumb`
// siblings, and a fourth time as a private copy in `PageView`. The live page
// painted its scrollbar from that private copy and was hit-tested against
// ui's, so the two agreeing was a coincidence rather than a fact.
inline constexpr float kScrollbarInsetX = 7.0f;
inline constexpr float kScrollbarInsetY = 9.0f;
inline constexpr float kScrollbarTrackWidth = 3.0f;
inline constexpr float kScrollbarThumbWidth = 5.0f;
inline constexpr float kScrollbarMinTrack = 24.0f;
inline constexpr float kScrollbarMinThumb = 22.0f;
// A thumb no shorter than this share of the track, so a very long note still
// leaves something to grab.
inline constexpr float kScrollbarMinThumbRatio = 0.08f;

// The page -- the writing surface itself -- inside the pane that holds it, and
// the measure of the text column on it.
//
// One set of numbers, because the surfaces that draw a page into a pane -- the
// page itself, in either mode, and the raw pane -- had the inset written out
// **seven** times: once each in `PageView` and the reading pane, twice in
// `RawPane`, and four times inline in `Application.cpp`'s hit tests. Two of
// those spellings were `h - kPagePad * 3.5` and `h - 28`, which are the same
// number for as long as nobody changes the padding.
inline constexpr float kPagePad = 8.0f;
// Taken off the bottom on top of `kPagePad`, so the page stops clear of the
// status bar rather than running under it.
inline constexpr float kPageBottomPad = 12.0f;
// The air either side of the text column inside the page.
inline constexpr float kPageColumnPad = 28.0f;
// The narrowest a measure is allowed to get before the page gives up centring
// it and lets it use the gutter's room instead.
inline constexpr float kPageMinColumn = 120.0f;

}
