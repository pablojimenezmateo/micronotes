#pragma once

#include <SDL3/SDL_stdinc.h>

// When the buffer is written back without being asked.
//
// Three questions, and the loop answered all three by hand: whether there is an
// edit worth writing, how long until it is due, and whether it is due now. The
// first was spelled out at three sites -- the wait, the loop's own test, and the
// save on the way out -- and the timings at two, as `lastEdit + 1201` in one and
// `now - lastEdit > 1200` in the other. Those two agree, but only because the
// `+1` compensates for the `>`, and nothing said so: a reader checking whether
// the wait and the fire were in step had to notice the off-by-one was
// deliberate. If they had ever disagreed the loop would have woken a
// millisecond early, found the save not yet due, drawn nothing, and slept
// again -- forever, at one wake per millisecond, with every test passing.
//
// Pure functions over the two clocks, so the schedule can be checked without a
// window. `docs/performance.md`'s eighth pass is about what an unbudgeted
// autosave costs; this is where the budget's *trigger* lives.
namespace micronotes::app {

struct UiRuntime;

// How long the typing has to stop before a save, and how far apart two saves
// must be. The quiet period is the one a reader feels: it is long enough that a
// pause for thought mid-sentence does not cost an fsync, and short enough that
// nothing is lost by walking away.
inline constexpr Uint64 kAutosaveQuietMs = 1200;
inline constexpr Uint64 kAutosaveIntervalMs = 1000;

// Whether there is an edit worth writing at all: a library, a note selected in
// it, and a buffer that has moved since it was last written. Nothing below this
// means anything without it, and the save on the way out asks only this.
bool autosavePending(const UiRuntime& ui);

// The tick the save comes due at. Both clocks, because a burst of typing keeps
// pushing the quiet period out while the interval holds the floor.
Uint64 autosaveDueAt(const UiRuntime& ui);

// Whether the save is due now.
bool autosaveDue(const UiRuntime& ui, Uint64 nowMs);

// Milliseconds until it comes due -- 0 when it is due already, and -1 when
// there is nothing to save and the loop need not wake for this at all.
int autosaveWaitMs(const UiRuntime& ui, Uint64 nowMs);

}
