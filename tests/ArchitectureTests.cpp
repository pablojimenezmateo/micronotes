#include "TestSupport.h"

#include "core/perf/PerformanceCounters.h"
#include "core/render/FontResolver.h"
#include "core/render/TextTextureCache.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Invariants about the shape of the source tree rather than the behaviour of
// any one function. They exist because the failure modes below are silent: the
// code compiles, the tests pass, and the damage only shows up as a number that
// quietly lies to whoever reads it next.

namespace {

std::filesystem::path repoRoot() {
  return std::filesystem::path(MICRONOTES_SOURCE_DIR);
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::vector<std::filesystem::path> sourceFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  if(!std::filesystem::exists(root)) return files;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if(!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension();
    if(extension == ".cpp" || extension == ".h") files.push_back(entry.path());
  }
  return files;
}

// Every id declared through an X(...) row in the given header.
std::vector<std::string> declaredCounterIds(const std::filesystem::path& header) {
  const std::string text = readText(header);
  std::vector<std::string> ids;
  const std::regex declaration(R"(\bX\(\s*(\w+)\s*,)");
  for(std::sregex_iterator it(text.begin(), text.end(), declaration), last; it != last; ++it) {
    ids.push_back((*it)[1].str());
  }
  return ids;
}

}

// A counter with no producer reads zero forever, which is strictly worse than
// no counter at all: a reader sees the row missing from a dump and concludes
// the code path did not run, when in fact nobody ever incremented it. Declaring
// an id costs one line and is easy to do while sketching; wiring it up is the
// part that gets forgotten.
MICRONOTES_TEST(architecture_every_perf_counter_has_a_producer) {
  const auto coreHeader = repoRoot() / "src/core/perf/PerformanceCounters.h";
  const auto appHeader = repoRoot() / "src/AppPerfCounters.h";
  MICRONOTES_REQUIRE(std::filesystem::exists(coreHeader));
  MICRONOTES_REQUIRE(std::filesystem::exists(appHeader));

  // If the X-macro spelling changes, this test would silently scan nothing and
  // pass forever. Anchor on it explicitly.
  MICRONOTES_REQUIRE(readText(coreHeader).find("#define MICROCORE_PERF_COUNTERS(X)") != std::string::npos);
  MICRONOTES_REQUIRE(readText(appHeader).find("#define MICROCORE_APP_PERF_COUNTERS(X)") != std::string::npos);

  std::vector<std::string> declared = declaredCounterIds(coreHeader);
  const auto appIds = declaredCounterIds(appHeader);
  declared.insert(declared.end(), appIds.begin(), appIds.end());
  MICRONOTES_REQUIRE(!declared.empty());

  std::set<std::string> produced;
  const std::regex use(R"(\bCounterId::(\w+))");
  for(const auto& path : sourceFiles(repoRoot() / "src")) {
    // The declaration and the name table are not producers.
    const auto name = path.filename().string();
    if(name == "PerformanceCounters.h" || name == "PerformanceCounters.cpp" || name == "AppPerfCounters.h") continue;
    const std::string text = readText(path);
    for(std::sregex_iterator it(text.begin(), text.end(), use), last; it != last; ++it) {
      produced.insert((*it)[1].str());
    }
  }

  std::string orphans;
  for(const auto& id : declared) {
    if(produced.count(id) == 0) {
      if(!orphans.empty()) orphans += ", ";
      orphans += id;
    }
  }
  micronotes::tests::require(
    orphans.empty(),
    "perf counters are declared but never incremented anywhere in src/: " + orphans +
    " -- either wire them up or delete them; a counter that reads zero forever is "
    "read as 'this code path did not run'");
}

// The counter table is indexed positionally by CounterId, so the enum and the
// name array must have the same length. The X-macro makes that true by
// construction; this test fails loudly if someone reintroduces a hand-written
// parallel list.
MICRONOTES_TEST(architecture_perf_counter_names_cover_every_id) {
  using microcore::perf::CounterId;
  using microcore::perf::counterName;
  for(std::size_t i = 0; i < microcore::perf::kCounterCount; ++i) {
    const auto name = counterName(static_cast<CounterId>(i));
    MICRONOTES_REQUIRE(!name.empty());
    // "<subsystem>.<event>" -- a name without a dot means a row was added
    // without following the convention the dumps are grouped by.
    MICRONOTES_REQUIRE(name.find('.') != std::string_view::npos);
  }
}

// src/core is vendored into microagenda byte-for-byte, so it must not name the
// app it happens to be compiled into. AppIdentity.h is the one seam, and it
// carries the name as a macro the host CMakeLists supplies.
MICRONOTES_TEST(architecture_core_does_not_hardcode_the_app_name) {
  std::string offenders;
  for(const auto& path : sourceFiles(repoRoot() / "src/core")) {
    if(path.filename() == "AppIdentity.h") continue;
    const std::string text = readText(path);
    if(text.find("micronotes") != std::string::npos || text.find("microagenda") != std::string::npos) {
      if(!offenders.empty()) offenders += ", ";
      offenders += path.filename().string();
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "src/core names a specific app: " + offenders +
    " -- use kAppName / kAppDotDir from core/AppIdentity.h so the vendored copy "
    "in microagenda stays byte-identical");
}

// --- core/render ------------------------------------------------------------

// Hardcoded /usr/share/fonts/truetype/dejavu/ paths meant a machine without
// DejaVu installed exactly there rendered no text at all: TTF_OpenFont returned
// null and every draw call silently did nothing.
MICRONOTES_TEST(font_resolver_finds_a_real_file_for_every_face) {
  const microcore::render::FontRequest requests[] = {
    {false, false, false}, {false, true, false}, {false, false, true},
    {false, true, true},   {true, false, false},
  };
  for(const auto& request : requests) {
    const auto path = microcore::render::resolveFontFile(request);
    micronotes::tests::require(!path.empty(), "no font resolved for a requested face");
    micronotes::tests::require(std::filesystem::exists(path), "resolved font does not exist: " + path);
  }
}

// The old cache destroyed every texture on reaching 4096 entries. Besides the
// one-frame cost of thousands of destructions, a full flush discards exactly
// the entries about to be reused, because it cannot tell which those are.
MICRONOTES_TEST(text_texture_cache_evicts_least_recently_used_not_everything) {
  microcore::render::TextTextureCache cache(3);
  const SDL_Color color {255, 255, 255, 255};
  const microcore::render::TextTextureCache::Style style {};
  // Null textures: the cache only ever destroys a non-null one, so this
  // exercises the eviction policy without needing a live renderer.
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};

  cache.insert("a", color, style, entry);
  cache.insert("b", color, style, entry);
  cache.insert("c", color, style, entry);
  MICRONOTES_REQUIRE(cache.size() == 3);

  // Touch "a" so "b" becomes the coldest entry.
  MICRONOTES_REQUIRE(cache.find("a", color, style) != nullptr);
  cache.insert("d", color, style, entry);

  MICRONOTES_REQUIRE(cache.size() == 3);
  MICRONOTES_REQUIRE(cache.find("b", color, style) == nullptr);  // evicted
  MICRONOTES_REQUIRE(cache.find("a", color, style) != nullptr);  // kept: recently used
  MICRONOTES_REQUIRE(cache.find("c", color, style) != nullptr);
  MICRONOTES_REQUIRE(cache.find("d", color, style) != nullptr);
}

MICRONOTES_TEST(text_texture_cache_distinguishes_colour_and_style) {
  microcore::render::TextTextureCache cache(16);
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color red {255, 0, 0, 255};

  cache.insert("x", white, {}, entry);
  MICRONOTES_REQUIRE(cache.find("x", red, {}) == nullptr);
  MICRONOTES_REQUIRE(cache.find("x", white, {true, false, false, false}) == nullptr);
  MICRONOTES_REQUIRE(cache.find("x", white, {}) != nullptr);
}
