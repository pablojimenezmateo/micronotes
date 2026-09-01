#pragma once

#include "ui/Theme.h"
#include "CoreAliases.h"

#include <filesystem>
#include <optional>
#include <string>

namespace micronotes::app {

struct ApplicationOptions {
  std::filesystem::path libraryRoot;
  std::optional<std::filesystem::path> configuredLibraryRoot;
  std::filesystem::path attachPath;
  bool headless = false;
  // Debug aid: render a single frame to an image file and exit. Keeps UI work
  // verifiable on Wayland, where external screenshot tools cannot reach the
  // window.
  std::filesystem::path screenshotPath;
  int windowWidth = 1280;
  int windowHeight = 800;
  std::optional<ui::ThemeMode> theme;
  // Overrides the display scale; 0 means "ask the window".
  float scale = 0.0f;
  // Debug aids for reproducible captures.
  std::optional<int> paneMode;
  std::string selectTitle;
  std::string openOverlay;
  // Which panels a captured frame should show, so a screenshot can pin an
  // arrangement the persisted state does not happen to be in.
  std::optional<bool> showSidebar;
  std::optional<bool> showNoteList;
  std::optional<bool> showRightPanel;
  // Which of the right panel's views a captured frame should be showing.
  std::string rightPanelView;
};

int run(ApplicationOptions options);
ApplicationOptions parseArgs(int argc, char** argv);

}
