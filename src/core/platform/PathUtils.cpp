#include "core/platform/PathUtils.h"

#include "core/AppIdentity.h"

#include <cstdlib>
#include <stdexcept>

namespace microcore::platform {

static std::filesystem::path homeDir() {
  if(const char* home = std::getenv("HOME"); home && *home) return home;
  return std::filesystem::current_path();
}

static std::filesystem::path xdgDir(const char* envName, const char* fallback) {
  if(const char* value = std::getenv(envName); value && *value) return value;
  return homeDir() / fallback;
}

RuntimePaths resolveRuntimePaths() {
  return {
    xdgDir("XDG_CONFIG_HOME", ".config") / kAppName,
    xdgDir("XDG_CACHE_HOME", ".cache") / kAppName,
    xdgDir("XDG_DATA_HOME", ".local/share") / kAppName,
  };
}

SafeRoot::SafeRoot(const std::filesystem::path& root)
  : canonical_(std::filesystem::weakly_canonical(root)) {}

std::filesystem::path SafeRoot::normalize(const std::filesystem::path& candidate) const {
  const auto candidateAbs = std::filesystem::weakly_canonical(candidate);
  auto rootIt = canonical_.begin();
  auto candidateIt = candidateAbs.begin();
  for(; rootIt != canonical_.end(); ++rootIt, ++candidateIt) {
    if(candidateIt == candidateAbs.end() || *rootIt != *candidateIt) {
      throw std::runtime_error("path escapes library root");
    }
  }
  return candidateAbs;
}

std::filesystem::path normalizeInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  return SafeRoot(root).normalize(candidate);
}

std::string sanitizeFileStem(std::string title) {
  std::string out;
  out.reserve(title.size());
  bool lastDash = false;
  for(char ch : title) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    if(ok) {
      out.push_back(ch);
      lastDash = false;
    } else if(!lastDash) {
      out.push_back('-');
      lastDash = true;
    }
  }
  while(!out.empty() && out.front() == '-') out.erase(out.begin());
  while(!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "untitled" : out;
}

}
