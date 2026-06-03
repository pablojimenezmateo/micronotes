#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace micronotes::app {

struct ApplicationOptions {
  std::filesystem::path libraryRoot;
  std::optional<std::filesystem::path> configuredLibraryRoot;
  std::filesystem::path attachPath;
  bool headless = false;
};

int run(ApplicationOptions options);
ApplicationOptions parseArgs(int argc, char** argv);

}
