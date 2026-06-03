#include "ui/ShellModel.h"

namespace micronotes::ui {

void PaneController::setMode(PaneMode mode) {
  mode_ = mode;
}

PaneMode PaneController::mode() const {
  return mode_;
}

bool PaneController::editorVisible() const {
  return mode_ == PaneMode::Editor || mode_ == PaneMode::Split;
}

bool PaneController::viewerVisible() const {
  return mode_ == PaneMode::Viewer || mode_ == PaneMode::Split;
}

DebouncedRefresh::DebouncedRefresh(int delayMs) : delayMs_(delayMs) {}

void DebouncedRefresh::markDirty(int nowMs) {
  dirtyAtMs_ = nowMs;
  dirty_ = true;
}

bool DebouncedRefresh::shouldRefresh(int nowMs) const {
  return dirty_ && dirtyAtMs_ >= 0 && nowMs - dirtyAtMs_ >= delayMs_;
}

void DebouncedRefresh::markRefreshed() {
  dirty_ = false;
  dirtyAtMs_ = -1;
}

}
