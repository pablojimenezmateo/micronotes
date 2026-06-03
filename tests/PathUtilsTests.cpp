#include "TestSupport.h"

#include "platform/PathUtils.h"

#include <filesystem>

MICRONOTES_TEST(path_utils_sanitizes_file_stem) {
  MICRONOTES_REQUIRE(micronotes::platform::sanitizeFileStem("Fast Notes!") == "Fast-Notes");
  MICRONOTES_REQUIRE(micronotes::platform::sanitizeFileStem("...") == "untitled");
}

MICRONOTES_TEST(path_utils_resolves_runtime_paths) {
  const auto paths = micronotes::platform::resolveRuntimePaths();
  MICRONOTES_REQUIRE(paths.configDir.string().find("micronotes") != std::string::npos);
  MICRONOTES_REQUIRE(paths.cacheDir.string().find("micronotes") != std::string::npos);
  MICRONOTES_REQUIRE(paths.dataDir.string().find("micronotes") != std::string::npos);
}
