#include "app/CursorTheme.h"

#include "CoreAliases.h"
#include "core/util/StringUtil.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

namespace micronotes::app {
namespace {

// How far an `Inherits=` chain is followed. A cycle is already stopped by the
// visited set; this bounds a pathological chain of stubs.
constexpr int kMaxInheritDepth = 8;

std::string envOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

// The `Inherits=` line of `<dir>/<theme>/index.theme`, split on commas. Xcursor
// itself is this permissive: the key may appear under any section, and the
// names may carry surrounding space.
std::vector<std::string> inheritedThemes(const std::filesystem::path& themeDir) {
  std::vector<std::string> parents;
  std::ifstream in(themeDir / "index.theme");
  if(!in) return parents;
  std::string line;
  while(std::getline(in, line)) {
    const std::string_view trimmed = util::trim(line);
    if(!trimmed.starts_with("Inherits")) continue;
    const auto equals = trimmed.find('=');
    if(equals == std::string_view::npos) continue;
    std::string_view list = trimmed.substr(equals + 1);
    while(!list.empty()) {
      const auto comma = list.find(',');
      const std::string_view piece = util::trim(list.substr(0, comma));
      if(!piece.empty()) parents.emplace_back(piece);
      if(comma == std::string_view::npos) break;
      list = list.substr(comma + 1);
    }
    break;
  }
  return parents;
}

// Whether any search directory holds `<theme>/cursors/<shape>`.
bool themeDirHasShape(const std::vector<std::filesystem::path>& iconDirs, std::string_view theme,
                      const std::string& shape) {
  for(const auto& dir : iconDirs) {
    std::error_code error;
    if(std::filesystem::exists(dir / theme / "cursors" / shape, error)) return true;
  }
  return false;
}

bool hasShapes(const std::vector<std::filesystem::path>& iconDirs, std::string_view theme,
               const std::vector<std::string>& shapes, std::set<std::string, std::less<>>& seen,
               int depth) {
  if(theme.empty() || depth > kMaxInheritDepth) return false;
  if(!seen.insert(std::string(theme)).second) return false;

  // Every shape has to be found, but not all in the same theme: a theme that
  // carries a `pointer` and inherits its `text` is a working theme.
  std::vector<std::string> missing;
  for(const auto& shape : shapes) {
    if(!themeDirHasShape(iconDirs, theme, shape)) missing.push_back(shape);
  }
  if(missing.empty()) return true;

  for(const auto& dir : iconDirs) {
    std::error_code error;
    const auto themeDir = dir / theme;
    if(!std::filesystem::exists(themeDir, error)) continue;
    for(const auto& parent : inheritedThemes(themeDir)) {
      if(hasShapes(iconDirs, parent, missing, seen, depth + 1)) return true;
    }
  }
  return false;
}

// Every theme with a `cursors/` directory, in search order and without
// duplicates. A theme installed twice is one candidate, and the first copy
// found is the one Xcursor would use.
std::vector<std::string> installedCursorThemes(const std::vector<std::filesystem::path>& iconDirs) {
  std::vector<std::string> themes;
  std::set<std::string, std::less<>> seen;
  for(const auto& dir : iconDirs) {
    std::error_code error;
    if(!std::filesystem::is_directory(dir, error)) continue;
    for(const auto& entry : std::filesystem::directory_iterator(dir, error)) {
      if(error) break;
      if(!entry.is_directory(error)) continue;
      if(!std::filesystem::is_directory(entry.path() / "cursors", error)) continue;
      const auto name = entry.path().filename().string();
      if(seen.insert(name).second) themes.push_back(name);
    }
  }
  std::sort(themes.begin(), themes.end());
  return themes;
}

// `gtk-cursor-theme-name` out of a GTK settings file.
std::string gtkCursorTheme(const std::filesystem::path& settings) {
  std::ifstream in(settings);
  if(!in) return {};
  std::string line;
  while(std::getline(in, line)) {
    const std::string_view trimmed = util::trim(line);
    if(!trimmed.starts_with("gtk-cursor-theme-name")) continue;
    const auto equals = trimmed.find('=');
    if(equals == std::string_view::npos) continue;
    return std::string(util::trim(trimmed.substr(equals + 1)));
  }
  return {};
}

}

const std::vector<std::string>& shellCursorShapes() {
  static const std::vector<std::string> shapes {"pointer", "text"};
  return shapes;
}

std::vector<std::filesystem::path> iconSearchPath() {
  std::vector<std::filesystem::path> dirs;
  const auto home = envOrEmpty("HOME");
  const auto dataHome = envOrEmpty("XDG_DATA_HOME");
  if(!dataHome.empty()) dirs.emplace_back(std::filesystem::path(dataHome) / "icons");
  else if(!home.empty()) dirs.emplace_back(std::filesystem::path(home) / ".local/share/icons");
  if(!home.empty()) dirs.emplace_back(std::filesystem::path(home) / ".icons");
  dirs.emplace_back("/usr/share/icons");
  dirs.emplace_back("/usr/local/share/icons");
  dirs.emplace_back("/usr/share/pixmaps");
  return dirs;
}

std::string configuredCursorTheme() {
  const auto home = envOrEmpty("HOME");
  if(home.empty()) return {};
  const std::filesystem::path config =
    envOrEmpty("XDG_CONFIG_HOME").empty() ? std::filesystem::path(home) / ".config"
                                          : std::filesystem::path(envOrEmpty("XDG_CONFIG_HOME"));
  // Newest first: a session running GTK 4 is the one whose setting is current.
  for(const char* version : {"gtk-4.0", "gtk-3.0"}) {
    const auto theme = gtkCursorTheme(config / version / "settings.ini");
    if(!theme.empty()) return theme;
  }
  return {};
}

bool cursorThemeHasShapes(const std::vector<std::filesystem::path>& iconDirs,
                          std::string_view theme, const std::vector<std::string>& shapes) {
  std::set<std::string, std::less<>> seen;
  return hasShapes(iconDirs, theme, shapes, seen, 0);
}

std::string chooseCursorTheme(const std::vector<std::filesystem::path>& iconDirs,
                              std::string_view configured, const std::vector<std::string>& shapes,
                              std::string_view preferred) {
  // Already chosen. Overriding it would be the app deciding it knows better
  // than the person who set the variable.
  if(!configured.empty()) return {};
  // What SDL would load if left alone. When it answers, there is nothing to do
  // -- and on a correctly configured desktop this is the path taken.
  if(cursorThemeHasShapes(iconDirs, "default", shapes)) return {};

  // The desktop's own setting, so the cursor matches the rest of the session
  // rather than merely working.
  if(!preferred.empty() && cursorThemeHasShapes(iconDirs, preferred, shapes)) {
    return std::string(preferred);
  }
  // Then the freedesktop default, which is the theme the broken chain should
  // have pointed at and the one a reader is least likely to find surprising.
  if(cursorThemeHasShapes(iconDirs, "Adwaita", shapes)) return "Adwaita";
  // Then anything installed, in name order so the choice does not depend on
  // the order a directory happens to be walked in.
  for(const auto& theme : installedCursorThemes(iconDirs)) {
    if(theme == "default") continue;
    if(cursorThemeHasShapes(iconDirs, theme, shapes)) return theme;
  }
  return {};
}

void ensureCursorTheme() {
  const auto chosen = chooseCursorTheme(iconSearchPath(), envOrEmpty("XCURSOR_THEME"),
                                        shellCursorShapes(), configuredCursorTheme());
  if(chosen.empty()) return;
  setenv("XCURSOR_THEME", chosen.c_str(), 1);
}

}
