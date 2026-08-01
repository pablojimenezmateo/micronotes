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
};

int run(ApplicationOptions options);
ApplicationOptions parseArgs(int argc, char** argv);

}
