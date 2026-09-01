#include "app/Application.h"

#include "ui/Theme.h"
#include "ui/WorkspaceModel.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

// Turning a command line into ApplicationOptions, and nothing else. Split out
// of Application.cpp because it shares none of that file's state: it reads argv
// and returns a struct, which also makes it the one part of startup that could
// be tested without a window.
namespace micronotes::app {

ApplicationOptions parseArgs(int argc, char** argv) {
  ApplicationOptions options;
  for(int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if(arg == "--headless") {
      options.headless = true;
    } else if(arg == "--library" && i + 1 < argc) {
      options.libraryRoot = argv[++i];
    } else if(arg == "--set-library" && i + 1 < argc) {
      options.configuredLibraryRoot = std::filesystem::path(argv[++i]);
    } else if(arg == "--attach" && i + 1 < argc) {
      options.attachPath = argv[++i];
    } else if(arg == "--screenshot" && i + 1 < argc) {
      options.screenshotPath = argv[++i];
    } else if(arg == "--size" && i + 1 < argc) {
      const std::string value = argv[++i];
      const auto x = value.find('x');
      if(x != std::string::npos) {
        options.windowWidth = std::max(320, std::atoi(value.substr(0, x).c_str()));
        options.windowHeight = std::max(240, std::atoi(value.substr(x + 1).c_str()));
      }
    } else if(arg == "--theme" && i + 1 < argc) {
      options.theme = ui::themeModeFromName(argv[++i]);
    } else if(arg == "--scale" && i + 1 < argc) {
      options.scale = static_cast<float>(std::atof(argv[++i]));
    } else if(arg == "--panels" && i + 1 < argc) {
      // A comma-separated list of the panels to show, so a capture pins an
      // arrangement rather than inheriting whatever the library was left in.
      const std::string value = argv[++i];
      options.showSidebar = value.find("sidebar") != std::string::npos;
      options.showNoteList = value.find("notes") != std::string::npos;
      options.showRightPanel = value.find("right") != std::string::npos;
    } else if(arg == "--right-panel" && i + 1 < argc) {
      options.rightPanelView = argv[++i];
      options.showRightPanel = true;
    } else if(arg == "--pane" && i + 1 < argc) {
      const std::string value = argv[++i];
      if(value == "editor" || value == "raw") options.paneMode = 0;
      else if(value == "viewer" || value == "reading") options.paneMode = 1;
      else if(value == "split") options.paneMode = 2;
      else if(value == "live") options.paneMode = 3;
    } else if(arg == "--select" && i + 1 < argc) {
      options.selectTitle = argv[++i];
    } else if(arg == "--open" && i + 1 < argc) {
      options.openOverlay = argv[++i];
    }
  }
  return options;
}

}
