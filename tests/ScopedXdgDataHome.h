#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace micronotes::tests {

// Points anything that reads `XDG_DATA_HOME` at a scratch directory, and puts
// back whatever was there on the way out.
//
// The recovery store and the index's own state directory are both derived from
// it, so a test that does not set it writes into the person's real one -- and
// then the next run of the app reads what a test left. It was declared inside
// `SearchTests.cpp`, which is why two other suites that need the same thing did
// without it.
class ScopedXdgDataHome {
public:
  explicit ScopedXdgDataHome(const std::filesystem::path& path) {
    if(const char* current = std::getenv("XDG_DATA_HOME")) {
      previous_ = current;
      hadPrevious_ = true;
    }
    setenv("XDG_DATA_HOME", path.c_str(), 1);
  }

  ~ScopedXdgDataHome() {
    if(hadPrevious_) setenv("XDG_DATA_HOME", previous_.c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
  }

  ScopedXdgDataHome(const ScopedXdgDataHome&) = delete;
  ScopedXdgDataHome& operator=(const ScopedXdgDataHome&) = delete;

private:
  std::string previous_;
  bool hadPrevious_ = false;
};

}
