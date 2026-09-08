#include "core/platform/PathUtils.h"

#include "core/AppIdentity.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

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


std::string displayPath(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  const char* home = std::getenv("HOME");
  if(!home || !*home) return text;
  const std::string prefix(home);
  if(text.rfind(prefix, 0) != 0) return text;
  if(text.size() == prefix.size()) return "~";
  if(text[prefix.size()] != '/') return text;
  return "~" + text.substr(prefix.size());
}


std::filesystem::path uniquePath(const std::filesystem::path& desired,
                                 const std::filesystem::path& keep) {
  std::error_code ec;
  if(!std::filesystem::exists(desired)) return desired;
  if(!keep.empty() && std::filesystem::equivalent(desired, keep, ec) && !ec) return desired;
  const auto parent = desired.parent_path();
  const auto stem = desired.stem().string();
  const auto extension = desired.extension().string();
  for(int suffix = 2;; ++suffix) {
    auto candidate = parent / (stem + "-" + std::to_string(suffix) + extension);
    if(!std::filesystem::exists(candidate)) return candidate;
  }
}

}
