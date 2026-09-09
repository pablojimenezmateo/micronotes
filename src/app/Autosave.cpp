#include "app/Autosave.h"

#include "app/Shell.h"

#include <algorithm>

namespace micronotes::app {

bool autosavePending(const UiRuntime& ui) {
  return ui.state.catalog().isOpen() && ui.editor.dirty() && !ui.state.selection().noteId.empty();
}

Uint64 autosaveDueAt(const UiRuntime& ui) {
  return std::max(ui.lastEdit + kAutosaveQuietMs, ui.lastAutosaveAttempt + kAutosaveIntervalMs) + 1;
}

bool autosaveDue(const UiRuntime& ui, Uint64 nowMs) {
  return autosavePending(ui) && nowMs >= autosaveDueAt(ui);
}

int autosaveWaitMs(const UiRuntime& ui, Uint64 nowMs) {
  if(!autosavePending(ui)) return -1;
  const Uint64 due = autosaveDueAt(ui);
  if(nowMs >= due) return 0;
  // The ceiling is the quiet period itself: nothing can be further out than one
  // whole quiet period from the keystroke that pushed it there.
  return std::clamp(static_cast<int>(due - nowMs), 1, static_cast<int>(kAutosaveQuietMs));
}

}
