#pragma once

// Which surfaces are drawn *over* the one about to be painted, and therefore
// own the press wherever it lands.
//
// A modal is drawn over the panels and over their washed-out copy of the
// window, and nothing under that wash may answer the pointer: a row that
// lights up behind a palette is promising a click the palette is going to
// swallow, and a hand cursor over a tab nobody can click is the same promise
// in another form. `../microide` draws the line the same way -- see its
// `MenuSurfaceCapturingMouse`, which suppresses exactly this.
//
// A ladder rather than one flag, because "modal" is relative to what is being
// drawn: the menu bar is under an overlay and under the Settings card but
// *not* under its own popup -- sliding along the bar with a menu open is how a
// menu bar switches menus, and a bar that stopped highlighting would stop
// looking like one.
//
// **A type rather than three booleans recombined at each call site**, and that
// is the whole reason this header exists. The combinations *are* the rule, and
// written out five times in the paint they were five places to get right: a
// fourth capturing surface means finding all five, and the failure is a row
// that highlights and cannot be clicked, which nothing fails on. Here each
// rung is named once, and `app/Frame.cpp` reads them in paint order.
//
// The order of the rungs below is the order of the paint. Keeping them in that
// order is not decoration -- it is what makes "each rung names what is above
// the surface that follows it" checkable by reading.
namespace micronotes::app {

struct UiRuntime;

class CaptureLadder {
public:
  // The three surfaces that capture, read off the shell once at the top of the
  // frame. Once, because they must not change under the paint: a surface
  // opened halfway down a frame would leave the rungs above it disagreeing
  // with the rungs below.
  explicit CaptureLadder(const UiRuntime& ui);

  // For the test, and for a reader who wants to see the three inputs rather
  // than infer them from the five answers.
  CaptureLadder(bool overlay, bool settingsCard, bool openMenu)
      : overlay_(overlay), card_(settingsCard), menu_(openMenu) {}

  // The menu bar, drawn first and above everything else in the window. Not
  // under its own popup; see above.
  bool menuBar() const { return overlay_ || card_; }
  // The sidebar, the tab strip, the breadcrumb, the panes, the right panel and
  // the status bar. Under all three.
  bool panel() const { return overlay_ || card_ || menu_; }
  // The menu bar's popup, drawn after every panel so it lands on top of
  // whichever one it hangs over.
  bool openMenu() const { return overlay_ || card_; }
  // The Settings card, under the overlay stack only -- one of its rows opens
  // the library prompt, and that prompt has to be usable over the card that
  // asked for it.
  bool settingsCard() const { return overlay_; }
  // The overlay stack itself, which nothing is above.
  bool overlays() const { return false; }

private:
  bool overlay_ = false;
  bool card_ = false;
  bool menu_ = false;
};

}
