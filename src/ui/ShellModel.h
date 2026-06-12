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

}
