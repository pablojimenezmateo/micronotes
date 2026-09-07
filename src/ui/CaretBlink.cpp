#include "ui/CaretBlink.h"

namespace micronotes::ui {

void CaretBlink::observe(std::uint64_t key, Uint64 nowMs) {
  if(observed_ && key == key_) return;
  observed_ = true;
  key_ = key;
  // The blink restarts from *now*, in its on-phase, so the first thing a
  // keystroke does is show a solid caret where the character landed.
  epoch_ = nowMs;
}

bool CaretBlink::visible(Uint64 nowMs) const {
  if(!observed_) return true;
  // A clock that has gone backwards -- which `SDL_GetTicks` will not do, but a
  // test driving this with its own numbers might -- reads as "just moved"
  // rather than as an enormous elapsed time.
  if(nowMs <= epoch_) return true;
  const Uint64 elapsed = nowMs - epoch_;
  // Settled: solid, and asking for no more wake-ups. Solid rather than hidden,
  // because the alternative is a caret that vanished and has nothing scheduled
  // to bring it back.
  if(elapsed >= kCaretBlinkSettleMs) return true;
  return ((elapsed / kCaretBlinkIntervalMs) % 2) == 0;
}

int CaretBlink::waitMs(Uint64 nowMs) const {
  if(!observed_) return -1;
  if(nowMs < epoch_) return -1;
  const Uint64 elapsed = nowMs - epoch_;
  if(elapsed >= kCaretBlinkSettleMs) return -1;
  // The next phase boundary, and the settle if that comes first: the frame the
  // blink stops on has to be drawn too, or a caret caught in an off-phase stays
  // off until something else happens to wake the window.
  const Uint64 nextPhase = (elapsed / kCaretBlinkIntervalMs + 1) * kCaretBlinkIntervalMs;
  const Uint64 next = nextPhase < kCaretBlinkSettleMs ? nextPhase : kCaretBlinkSettleMs;
  // Never zero: `chooseWait` floors a timeout at 1ms anyway, and returning 0
  // from here would read as "a deadline that has passed" at the call site.
  return static_cast<int>(next - elapsed) > 0 ? static_cast<int>(next - elapsed) : 1;
}

}
