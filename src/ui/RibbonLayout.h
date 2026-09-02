#pragma once

#include "ui/Actions.h"
#include "ui/Rect.h"

#include <array>
#include <cstddef>
#include <span>

namespace micronotes::ui {

// Which controls the icon rail shows, and where each one sits.
//
// Split out of the paint because it is arithmetic with a rule in it, and the
// rule is the interesting part: a window too short for the whole rail has to
// give some controls up, and which ones it gives up decides whether a shell
// with every panel hidden can still be got out of with the mouse. The draw and
// both hit tests read this one answer, so what was painted and what a click
// lands on cannot disagree -- and the rule can be tested without a renderer.

// The mark drawn on a control. Named alongside the action rather than in the
// paint, so the table below is one row per control and nothing has to be kept
// in step with it.
//
// Drawn rather than typeset, every one of them: the vendored UI face has no
// glyph for a page, a magnifier or a pane, and the emoji face is not installed
// everywhere. A mark assembled from lines and rects is the only one that is
// certain to be there and certain to stay crisp at any display scale.
enum class RibbonMark {
  NewNote,
  GoToNote,
  Search,
  Commands,
  LeftPanel,
  RightPanel,
  Settings
};

struct RibbonControl {
  ActionId action = ActionId::Count;
  RibbonMark mark = RibbonMark::NewNote;
  // Drawn up from the bottom edge rather than down from the top. The foot holds
  // the arrangement of the window and the way into its settings, because they
  // are what you reach for having finished, not started.
  bool atFoot = false;
};

// Seven, and the number is spelt here so a caller can size a buffer for the
// lot rather than allocating one per frame.
inline constexpr std::size_t kRibbonControlCount = 7;

// Every control the rail can show, top group first.
//
// Exposed because it is the list of things the rail offers, and more than the
// paint needs to know it: a test that every control the rail can be clicked on
// actually runs something reads this rather than a second copy of it.
std::span<const RibbonControl> ribbonControls();

struct PlacedRibbonControl {
  RibbonControl control;
  Rect rect;
};

// The controls that fit, with their boxes, in the order they are drawn.
//
// Iterable rather than handing back a span over its own storage: written as
// `for(const auto& placed : ribbonLayout(rect))` the temporary is the range and
// lives for the whole loop, whereas a span taken from that temporary dangles
// before the first iteration reads it.
struct RibbonPlacement {
  std::array<PlacedRibbonControl, kRibbonControlCount> controls {};
  std::size_t count = 0;

  const PlacedRibbonControl* begin() const { return controls.data(); }
  const PlacedRibbonControl* end() const { return controls.data() + count; }
  std::size_t size() const { return count; }
};

// Holds fewer than `kRibbonControlCount` controls when the column is too short,
// never more, and never two boxes over the same pixels: overlapping two marks
// would leave a smear that answers to whichever hit test ran last, and a
// control that is not there is at least honest about it.
RibbonPlacement ribbonLayout(Rect rect);

}
