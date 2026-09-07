#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstdint>

namespace micronotes::ui {

// Whether the caret is painted this instant.
//
// Every caret in the shell was drawn solid: the page's, the search box's, and
// the field in every overlay. A solid bar is a *character*, not an insertion
// point -- in a mono chrome face it reads as a pipe -- so the one piece of the
// interface whose whole job is to say "your typing goes here" said it in the
// same voice as the text around it. Which field had the keyboard was then
// something the reader worked out from the frame colour, if they noticed it.
//
// The rules are the ones every text field on the platform follows, and both
// halves matter:
//
//  * it blinks, so it is the only thing on screen that moves and the eye finds
//    it without being told where to look;
//  * it is **solid while you are typing**, and solid again once you stop for
//    long enough. A caret blinking under a moving cursor is a caret fighting
//    the text; a caret blinking forever is a window that never sleeps.
//
// That last rule is why this exists as a class rather than a modulo of the
// clock. The blink has to restart on every keystroke and every caret move, and
// settle after a while, so it needs to know when the caret last did something.

// The interval, and how long the blinking goes on for.
//
// 530 ms is X11's and GTK's default half-period, so a caret here keeps time
// with the rest of the desktop. Eight seconds of it is long enough that a
// reader who looked away and back still finds the caret, and short enough that
// a window left open does not wake sixty times a minute forever -- which is
// what the settle exists for: see `waitMs`.
inline constexpr Uint64 kCaretBlinkIntervalMs = 530;
inline constexpr Uint64 kCaretBlinkSettleMs = 8000;

class CaretBlink {
public:
  // Called once per frame with a value that changes whenever the caret does --
  // it moves, the text under it changes, or the focus moves to another field.
  //
  // A key rather than a `restart()` at every site that could move a caret, for
  // the reason the rest of this shell prefers keys: there are a dozen such
  // sites, the list grows, and the one that forgets leaves a caret blinking
  // through a burst of typing. A key cannot be forgotten, only unequal.
  void observe(std::uint64_t key, Uint64 nowMs);

  // Whether to paint it. True for the whole of the settle period's off-phases
  // only when the caret is between blinks; after the settle it is always true,
  // so a caret never ends up invisible because nothing woke to turn it back on.
  bool visible(Uint64 nowMs) const;

  // Milliseconds until the next phase change, or -1 when there will not be one.
  //
  // The -1 is the whole point of the settle: an idle window stops asking to be
  // woken, so a session left open costs nothing. Fed to `FrameDeadlines`, whose
  // `IdleHint::Blinking` was written for this and had no caller until now.
  int waitMs(Uint64 nowMs) const;

private:
  std::uint64_t key_ = 0;
  Uint64 epoch_ = 0;
  bool observed_ = false;
};

}
