#include "app/Pending.h"

#include "app/Autosave.h"
#include "app/CaretPolicy.h"
#include "app/Commands.h"
#include "app/ExportPdf.h"
#include "app/Notes.h"
#include "app/Shell.h"
#include "app/WindowChrome.h"

namespace micronotes::app {

bool applyPendingWork(SDL_Window* window, UiRuntime& ui, bool& running, Uint64 nowMs) {
  bool changed = false;
  if(applyPendingWindowAction(window, ui, running)) changed = true;
  // A note re-read from disk before the autosave below, so a file changed by
  // another program is never written back over from a buffer that predates it.
  if(applyWatchedChanges(ui)) changed = true;
  // See `app/ExportState.h`: the chooser answers on another thread and this is
  // where the answer is picked up.
  if(applyPendingExport(ui)) changed = true;
  // The one change with no event and no other actor behind it.
  if(caretPhaseChanged(ui)) changed = true;
  if(autosaveDue(ui, nowMs)) {
    ui.lastAutosaveAttempt = nowMs;
    (void)saveCurrent(ui, true);
    changed = true;
  }
  return changed;
}

}
