#pragma once

#include "ui/Rect.h"

namespace micronotes::ui {

// How much room the shell has to work with. `Compact` is what a narrow window
// gets: the side panels drop to their compact widths so the page keeps a usable
// column instead of being squeezed to nothing.
enum class LayoutMode {
  Regular,
  Compact
};

// Everything computeShellLayout() reads, and nothing else.
//
// The layout is a pure function of this struct, which is what lets the shell
// memoize it by comparing inputs rather than by trusting a dirty flag. A flag
// has to be raised at every site that changes any of these fields, and one
// missed site leaves a hit test answering for a rect that is no longer painted
// -- the kind of wrong that is invisible until someone clicks the wrong thing.
// A key cannot be missed, only unequal.
struct ShellLayoutInputs {
  float windowWidth = 0.0f;
  float windowHeight = 0.0f;

  // A hidden panel keeps its width so that showing it again restores the size
  // it had, rather than snapping back to the default.
  bool sidebarVisible = true;
  bool noteListVisible = true;
  bool rightPanelVisible = false;
  bool tabStripVisible = false;

  float sidebarWidth = 0.0f;
  float noteListWidth = 0.0f;
  float rightPanelWidth = 0.0f;

  // The mode the previous frame settled on. Feeding it back in is what gives
  // the compact breakpoint its hysteresis while keeping the function pure.
  LayoutMode previousMode = LayoutMode::Regular;

  friend bool operator==(const ShellLayoutInputs&, const ShellLayoutInputs&) = default;
};

// Where every region of the shell is drawn and hit-tested.
//
// A region that is not showing comes back empty rather than absent, so a caller
// can lay out, paint and hit-test against the same rect unconditionally and let
// the clip do the work.
struct ShellLayout {
  Rect sidebar;      // notebooks, favorites, tags
  Rect notes;        // search box and the note list
  Rect tabs;         // the tab strip above the page
  Rect crumbs;       // the breadcrumb trail
  Rect content;      // the page itself
  Rect rightPanel;   // outline, backlinks
  Rect status;       // the status bar, full width under everything

  LayoutMode mode = LayoutMode::Regular;

  friend bool operator==(const ShellLayout&, const ShellLayout&) = default;
};

// Which mode a window of this width settles in, given the mode it was already
// in. Exposed for the tests; computeShellLayout applies it itself.
LayoutMode resolveLayoutMode(float windowWidth, LayoutMode previous);

// Panel widths are clamped in here rather than by the caller, so that the hit
// tests, the cursor shape, the redraw rects and the tests can never disagree
// with what was actually painted.
ShellLayout computeShellLayout(const ShellLayoutInputs& inputs);

}
