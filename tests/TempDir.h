#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace micronotes::tests {

// A directory under the system temp root that cleans itself up.
//
// Every test that touches the disk opened with the same two lines -- name a
// path under `temp_directory_path()`, then `remove_all` whatever a previous run
// left there -- and ninety-seven of them ended with a third line removing it
// again. That third line is the one worth replacing: `MICRONOTES_REQUIRE`
// throws, so a test that *failed* never reached it and left its tree behind,
// which is exactly the run whose leftovers someone then goes looking at. A
// destructor runs on the way out either way.
//
// The constructor removes rather than creates, which is what the two lines it
// replaces did: several callers want to hand the path to something whose job is
// to create it -- `library::Library`, `AppState::openOrCreateLibrary` -- and a
// directory that already exists is a different test.
class TempDir {
public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  ~TempDir() {
    remove();
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  // Movable, so a fixture helper can build one and hand it back. The moved-from
  // path is cleared rather than left behind: two objects naming one directory
  // would delete it when the first of them went out of scope, which for a
  // fixture returned into a test is immediately.
  TempDir(TempDir&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
  }
  TempDir& operator=(TempDir&& other) noexcept {
    if(this != &other) {
      remove();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  const std::filesystem::path& path() const { return path_; }
  operator const std::filesystem::path&() const { return path_; }
  std::filesystem::path operator/(std::string_view child) const { return path_ / child; }

private:
  // Never throws: this runs during stack unwinding from a failed assertion, and
  // a destructor that threw there would replace the failure with a different
  // one. An empty path is a moved-from object and owns nothing.
  void remove() noexcept {
    if(path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  std::filesystem::path path_;
};

}
