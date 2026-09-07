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

// The menu bar along the very top of the window, full width above every panel:
// the menus, the application's name, and the window controls this borderless
// window draws for itself. Full width rather than over the page alone, because
// the close button has to sit in the actual corner of the window -- a control
// inset from the corner is one the pointer cannot be thrown at.
inline constexpr float kMenuBarHeight = 25.0f;

// Horizontal strips across the content column, top to bottom. The tab strip
// and the breadcrumb belong to the *page*, not to the window: a strip that
// spans the window makes the band above the sidebar change owner depending on
// what is open, which is what the panel edge is there to prevent.
inline constexpr float kTabStripHeight = 34.0f;
inline constexpr float kBreadcrumbHeight = 26.0f;

// The status bar, full width under everything.
inline constexpr float kStatusBarHeight = 22.0f;

// Between the panes: one weight, drawn once. A shell with a "strong" and a
// "subtle" separator has two grids that do not line up.
inline constexpr float kDividerThickness = 1.0f;

// The drawn window controls: minimise, maximise, close, laid out left to right
// so close ends nearest the corner. Square, at whatever height the menu bar
// leaves them, so they scale with the bar rather than against it.
inline constexpr float kWindowButtonGap = 4.0f;
inline constexpr float kWindowButtonRightInset = 8.0f;
inline constexpr float kWindowButtonHitInflate = 2.0f;

// How wide the invisible resize border around a borderless window is. The hit
// test and nothing else reads this, but it lives here because it is a chrome
// size like the rest.
inline constexpr float kWindowFrameThickness = 6.0f;

// The mark in a callout's gutter. Both surfaces are the same renderer now, so
// this has one reader -- but it stays named, because `7.0f` and `+ 6.0f` written
// out inline is how it came to be two shapes in two files in the first place.
inline constexpr float kCalloutMarkSize = 7.0f;
inline constexpr float kCalloutMarkInset = 6.0f;

// The menu bar's own geometry: a menu's label with air either side, clamped so
// a one-word menu is still a comfortable target and a long one cannot push the
// rest of the bar off the window. The chevron is what the menus that did not
// fit hide behind.
inline constexpr float kMenuBarItemPadding = 28.0f;
inline constexpr float kMenuBarItemMinWidth = 56.0f;
inline constexpr float kMenuBarItemMaxWidth = 116.0f;
inline constexpr float kMenuBarItemGap = 4.0f;
inline constexpr float kMenuBarEdgeInset = 8.0f;
inline constexpr float kMenuBarItemInsetY = 3.0f;
inline constexpr float kMenuOverflowChevronWidth = 28.0f;

// A menu popup: a row per item, a gap where a separator is, and the width the
// widest label-plus-accelerator asks for between a floor and the window.
inline constexpr float kMenuPopupItemHeight = 22.0f;
inline constexpr float kMenuPopupSeparatorHeight = 8.0f;
inline constexpr float kMenuPopupMinWidth = 172.0f;
inline constexpr float kMenuPopupPadY = 6.0f;
// What a row reserves left of its label for the check mark, and right of the
// accelerator for the edge.
inline constexpr float kMenuPopupLabelInset = 24.0f;
inline constexpr float kMenuPopupAcceleratorInset = 10.0f;
inline constexpr float kMenuPopupTextReserve = 68.0f;

// The band a callout reserves above its first line for the kind's icon and
// name. Layout reserves it and the draw fills it, so the number is here rather
// than in either of them.
inline constexpr float kCalloutTitleHeight = 24.0f;

// Panel widths a fresh library starts with. The sidebar is wider than a plain
// tree needs because it also holds the search field and its results.
inline constexpr float kDefaultSidebarWidth = 288.0f;
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

// The strip of accent down the leading edge of a selected row -- the shape of a
// marker in a margin. Hover lifts the row's ground and nothing else, so this is
// the whole of what says "this one" rather than "this is what I would click".
inline constexpr float kRowAccentWidth = 2.0f;

// The tree's own geometry: a step per level of depth, the slot at the head of a
// row that holds either a folder's chevron or a note's icon, and the inset the
// whole column starts at.
//
// One set of numbers for the file tree and for every other row in the panel,
// because a section heading, a tag, a search result and a tree row are four
// kinds of row in one column and they have to start their text at the same x.
inline constexpr float kTreeIndentWidth = 14.0f;
inline constexpr float kTreeChevronSlotWidth = 12.0f;
inline constexpr float kTreeLabelGap = 4.0f;
inline constexpr float kSidebarInset = 10.0f;

// The row height a tree of one-line rows wants. A floor rather than a constant:
// the rows hold text, and text grows with the reader's text size, so a row
// nailed to this number is one that draws its label into the row beneath it at
// the large size.
inline constexpr float kSidebarRowHeight = 20.0f;

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

// The vertical scrollbar every scrolling surface draws: a track inset from the
// viewport's trailing edge, and a thumb that fills it.
//
// A visible track with a thumb filling its whole width, rather than the hairline
// track and narrower thumb this replaces. A resting thumb has to read as
// grabbable at a glance, and a 5px handle on a 3px rail does not -- it reads as
// decoration until the pointer is already on it.
//
// Here because the numbers were written out four times: twice inside
// `ui::drawVerticalScrollbar` and its `scrollbarTrack`/`scrollbarThumb`
// siblings, and a fourth time as a private copy in `PageView`. The live page
// painted its scrollbar from that private copy and was hit-tested against
// ui's, so the two agreeing was a coincidence rather than a fact.
inline constexpr float kScrollbarThickness = 10.0f;
inline constexpr float kScrollbarInset = 2.0f;
inline constexpr float kScrollbarMinThumbLength = 24.0f;

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
