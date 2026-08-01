#pragma once

#include <string>
#include <vector>

namespace microcore::ui {

enum class PaneMode {
  Editor,   // raw Markdown source
  Viewer,   // read-only md4c rendering
  Split,    // source beside rendering
  Live      // formatting rendered in place, and editable
};

struct ShellModel {
  PaneMode paneMode = PaneMode::Live;
  int sidebarWidth = 240;
  int noteListWidth = 300;
  // Notes pinned to the top of the sidebar, and the ones opened most recently,
  // newest first. Both name notes by id and never touch a file: which notes
  // someone keeps to hand is a view preference, not part of the note.
  std::vector<std::string> favorites;
  std::vector<std::string> recents;
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
