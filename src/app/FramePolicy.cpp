#include "app/FramePolicy.h"

#include "app/Autosave.h"
#include "app/CaretPolicy.h"
#include "app/Shell.h"

namespace micronotes::app {

FrameDeadlines frameDeadlines(UiRuntime& ui, Uint64 nowMs) {
  FrameDeadlines out;
  out.autosaveMs = autosaveWaitMs(ui, nowMs);
  // `IdleHint::Blinking` and `caretBlinkMs` were written for this and had no
  // caller: every caret was drawn solid, so nothing ever needed waking.
  out.caretBlinkMs = settleCaret(ui);
  // A message on the status bar clears itself after a few seconds, and nothing
  // else moves when it does. Without a deadline the shell -- which sleeps until
  // something happens -- would keep showing it until the next keystroke.
  out.notificationMs = ui.status.lingerMs(nowMs);
  // A drag past the edge of a list has to keep scrolling while the pointer is
  // perfectly still, which produces no events at all.
  out.hint = ui.textSelect.active || ui.sidebar.drag.active() || ui.blockDrag.active
               ? IdleHint::Busy
             : out.caretBlinkMs >= 0 ? IdleHint::Blinking
                                     : IdleHint::Idle;
  return out;
}

}
