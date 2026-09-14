#include "app/Application.h"

#include <cstdio>

int main(int argc, char** argv) {
  auto options = micronotes::app::parseArgs(argc, argv);
  // Before run(), which opens a window. `micronotes --version` on a machine with
  // no display is how a packaged artifact is checked -- see
  // scripts/ci/verify-deb-runtime.sh -- so it must not need one.
  if(options.printVersion) {
    std::printf("%s\n", micronotes::app::versionLine().c_str());
    return 0;
  }
  return micronotes::app::run(std::move(options));
}
