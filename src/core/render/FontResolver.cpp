#include "core/render/FontResolver.h"

#include <array>
#include <filesystem>
#include <map>
#include <string_view>

#if MICROCORE_HAS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace microcore::render {
namespace {

// Ordered best-first. Only consulted when fontconfig is unavailable, and only
// as filenames to look for under the directories below.
constexpr std::array<std::string_view, 6> kSansCandidates = {
  "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf",
  "FreeSans.ttf", "Ubuntu-R.ttf", "Cantarell-Regular.otf",
};

constexpr std::array<std::string_view, 5> kMonoCandidates = {
  "DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf",
  "FreeMono.ttf", "UbuntuMono-R.ttf",
};

constexpr std::array<std::string_view, 4> kFontDirectories = {
  "/usr/share/fonts", "/usr/local/share/fonts", "/run/current-system/sw/share/X11/fonts",
  "/usr/share/fonts/truetype",
};

// Style suffixes to try in place of "Regular"/"" when a bold or italic face is
// wanted. Crude, but it only runs when fontconfig is missing.
std::string styledName(std::string_view base, const FontRequest& request) {
  if(!request.bold && !request.italic) return std::string(base);
  std::string name(base);
  const auto dot = name.rfind('.');
  const std::string extension = dot == std::string::npos ? "" : name.substr(dot);
  std::string stem = dot == std::string::npos ? name : name.substr(0, dot);

  const auto replaceSuffix = [&stem](std::string_view from, std::string_view to) {
    if(stem.size() >= from.size() && stem.compare(stem.size() - from.size(), from.size(), from) == 0) {
      stem.replace(stem.size() - from.size(), from.size(), to);
      return true;
    }
    return false;
  };

  std::string suffix;
  if(request.bold && request.italic) suffix = "BoldOblique";
  else if(request.bold) suffix = "Bold";
  else suffix = "Oblique";

  if(!replaceSuffix("-Regular", "-" + suffix)) stem += "-" + suffix;
  return stem + extension;
}

std::string scanForFile(std::string_view filename) {
  std::error_code error;
  for(const auto directory : kFontDirectories) {
    if(!std::filesystem::is_directory(directory, error)) continue;
    // recursive_directory_iterator with an error_code overload: a font
    // directory containing a dangling symlink must not throw and kill startup.
    std::filesystem::recursive_directory_iterator it(directory,
      std::filesystem::directory_options::skip_permission_denied, error);
    if(error) continue;
    const std::filesystem::recursive_directory_iterator end;
    for(; it != end; it.increment(error)) {
      if(error) break;
      if(!it->is_regular_file(error)) continue;
      if(it->path().filename() == filename) return it->path().string();
    }
  }
  return {};
}

#if MICROCORE_HAS_FONTCONFIG
std::string resolveWithFontconfig(const FontRequest& request) {
  FcConfig* config = FcInitLoadConfigAndFonts();
  if(!config) return {};

  FcPattern* pattern = FcPatternCreate();
  if(!pattern) {
    FcConfigDestroy(config);
    return {};
  }
  // Ask by generic family so fontconfig applies the user's own aliasing rules,
  // which is what makes this agree with the rest of the desktop.
  FcPatternAddString(pattern, FC_FAMILY,
                     reinterpret_cast<const FcChar8*>(request.monospace ? "monospace" : "sans-serif"));
  FcPatternAddInteger(pattern, FC_WEIGHT, request.bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
  FcPatternAddInteger(pattern, FC_SLANT, request.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
  // Only scalable outlines: a bitmap face gives TTF_OpenFont nothing to scale
  // and renders at one fixed size regardless of what was requested.
  FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);

  FcConfigSubstitute(config, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  std::string path;
  FcResult result {};
  if(FcPattern* matched = FcFontMatch(config, pattern, &result)) {
    FcChar8* file = nullptr;
    if(FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch && file) {
      path = reinterpret_cast<const char*>(file);
    }
    FcPatternDestroy(matched);
  }

  FcPatternDestroy(pattern);
  FcConfigDestroy(config);
  return path;
}
#endif

std::string resolveUncached(const FontRequest& request) {
#if MICROCORE_HAS_FONTCONFIG
  if(std::string path = resolveWithFontconfig(request); !path.empty()) return path;
#endif
  // Styled variant first, then the plain face: a missing bold is better served
  // by the regular weight than by no text at all.
  const auto tryAll = [&request](const auto& candidates) -> std::string {
    for(const auto candidate : candidates) {
      if(std::string path = scanForFile(styledName(candidate, request)); !path.empty()) return path;
    }
    for(const auto candidate : candidates) {
      if(std::string path = scanForFile(candidate); !path.empty()) return path;
    }
    return {};
  };
  return request.monospace ? tryAll(kMonoCandidates) : tryAll(kSansCandidates);
}

}

bool hasFontconfig() {
#if MICROCORE_HAS_FONTCONFIG
  return true;
#else
  return false;
#endif
}

std::string resolveFontFile(const FontRequest& request) {
  // Resolution walks the font graph (or the filesystem), which is far too
  // expensive to repeat per face per launch; the answer cannot change within a
  // run, so memoize on the request.
  const int key = (request.monospace ? 4 : 0) | (request.bold ? 2 : 0) | (request.italic ? 1 : 0);
  static std::map<int, std::string> cache;
  const auto found = cache.find(key);
  if(found != cache.end()) return found->second;
  auto path = resolveUncached(request);
  cache.emplace(key, path);
  return path;
}

}
