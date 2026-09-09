#pragma once

#include "app/Shell.h"

#include <cstdint>

// When the caret is solid, when it blinks, and when the loop has to wake for it.
//
// `ui::CaretBlink` is the mechanism; this is the shell's side of it -- what
// counts as the caret having moved, and which of the two carets on screen in a
// split view is being asked about. It was three inline functions in
// `app/Shell.h`, which meant the header describing the shell's *state* also
// carried a frame policy, and every file that wanted to name `UiRuntime` was
// handed the caret's schedule as well.
namespace micronotes::app {

// What the caret is doing, as one value.
//
// Fed to `ui::CaretBlink` once a frame; when it changes, the blink restarts in
// its on-phase, so a keystroke shows a solid caret where the character landed.
//
// A key rather than a `restartBlink()` at every site that could move a caret.
// There are a dozen of those -- every arrow key, every click into text, every
// edit, every focus change, the overlay stack's own field -- the list grows,
// and the one that forgets leaves a caret blinking through a burst of typing.
// The same discipline the sidebar's row memo and the page header use, and for
// the same reason: a key cannot be forgotten, only unequal.
std::uint64_t caretStateKey(const UiRuntime& ui);

// Settles the caret's blink for this frame, and reports how long until the next
// phase change -- or -1 once it has settled solid and nothing needs waking.
//
// Called from both the frame and the wait, and idempotent within a frame:
// observing the same key twice is a no-op, and both callers want the answer for
// the clock as it is now. Once rather than at each caret's paint, because two
// carets are on screen at once in a split view and they must not blink out of
// step.
int settleCaret(UiRuntime& ui);

// Whether the caret's blink has flipped since the last frame painted one.
//
// The run loop repaints on events, on a window action, on a watched change and
// on an autosave -- and the caret is behind none of those. So a blink wake
// arrived, found nothing to do, and counted a skipped repaint: the deadline was
// honoured and the frame it existed for was never drawn.
bool caretPhaseChanged(UiRuntime& ui);

}
