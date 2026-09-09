#include "CoreAliases.h"
#include "TestSupport.h"
#include "TempDir.h"

#include "core/platform/PathUtils.h"

#include <filesystem>
#include <stdexcept>

MICRONOTES_TEST(path_utils_sanitizes_file_stem) {
  MICRONOTES_REQUIRE(microcore::platform::sanitizeFileStem("Fast Notes!") == "Fast-Notes");
  MICRONOTES_REQUIRE(microcore::platform::sanitizeFileStem("...") == "untitled");
}

MICRONOTES_TEST(path_utils_resolves_runtime_paths) {
  const auto paths = microcore::platform::resolveRuntimePaths();
  MICRONOTES_REQUIRE(paths.configDir.string().find("micronotes") != std::string::npos);
  MICRONOTES_REQUIRE(paths.cacheDir.string().find("micronotes") != std::string::npos);
  MICRONOTES_REQUIRE(paths.dataDir.string().find("micronotes") != std::string::npos);
}

namespace {

// Whether `normalize` refused. The check reports an escape by throwing, so a
// test for it has to catch rather than compare.
bool refuses(const microcore::platform::SafeRoot& root, const std::filesystem::path& candidate) {
  try {
    (void)root.normalize(candidate);
    return false;
  } catch(const std::runtime_error&) {
    return true;
  }
}

}

// The containment check is what stops a note path -- which can come from a
// wikilink, a front-matter field or a file somebody dropped in the tree -- from
// addressing anything outside the library. It had no test of its own at all,
// which is a poor state for the one function here whose failure is a security
// failure rather than a wrong pixel.
MICRONOTES_TEST(safe_root_accepts_paths_inside_the_root_and_refuses_the_rest) {
  const micronotes::tests::TempDir baseDir("micronotes-safe-root-test");
  const auto& base = baseDir.path();
  std::filesystem::create_directories(base / "library" / "work");
  std::filesystem::create_directories(base / "elsewhere");
  const microcore::platform::SafeRoot root(base / "library");

  // Inside, and canonical on the way out.
  const auto inside = root.normalize(base / "library" / "work" / "note.md");
  MICRONOTES_REQUIRE(inside == std::filesystem::weakly_canonical(base / "library" / "work" / "note.md"));
  // A file that does not exist yet is still a path inside the root: this is
  // asked before a note is created, not only before one is read.
  MICRONOTES_REQUIRE(!refuses(root, base / "library" / "not-yet.md"));

  // Out, by climbing.
  MICRONOTES_REQUIRE(refuses(root, base / "library" / ".." / "elsewhere" / "note.md"));
  MICRONOTES_REQUIRE(refuses(root, base / "elsewhere" / "note.md"));
  MICRONOTES_REQUIRE(refuses(root, "/etc/passwd"));

  // And out by sharing a prefix. `/…/library-backup` starts with the root's
  // characters and is not inside it -- which is why the comparison walks path
  // components rather than bytes, and why this case is worth pinning down.
  std::filesystem::create_directories(base / "library-backup");
  MICRONOTES_REQUIRE(refuses(root, base / "library-backup" / "note.md"));

}

// The free function is the same check for a caller with nowhere to keep a
// SafeRoot, and it has to stay the same check.
MICRONOTES_TEST(normalize_inside_root_agrees_with_a_safe_root_built_from_the_same_path) {
  const micronotes::tests::TempDir baseDir("micronotes-safe-root-free");
  const auto& base = baseDir.path();
  std::filesystem::create_directories(base / "library");
  const microcore::platform::SafeRoot root(base / "library");
  const auto candidate = base / "library" / "note.md";
  MICRONOTES_REQUIRE(microcore::platform::normalizeInsideRoot(base / "library", candidate) ==
                     root.normalize(candidate));
  bool threw = false;
  try {
    (void)microcore::platform::normalizeInsideRoot(base / "library", base / "outside.md");
  } catch(const std::runtime_error&) {
    threw = true;
  }
  MICRONOTES_REQUIRE(threw);
}
