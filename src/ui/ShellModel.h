#pragma once

namespace micronotes::ui {

enum class PaneMode {
  Editor,
  Viewer,
  Split
};

struct ShellModel {
  PaneMode paneMode = PaneMode::Split;
  int sidebarWidth = 240;
  int noteListWidth = 300;
};

class PaneController {
public:
  void setMode(PaneMode mode);
  PaneMode mode() const;
  bool editorVisible() const;
  bool viewerVisible() const;

private:
  PaneMode mode_ = PaneMode::Split;
};

class DebouncedRefresh {
public:
  explicit DebouncedRefresh(int delayMs);
  void markDirty(int nowMs);
  bool shouldRefresh(int nowMs) const;
  void markRefreshed();

private:
  int delayMs_ = 0;
  int dirtyAtMs_ = -1;
  bool dirty_ = false;
};

}
