#include "CoreAliases.h"
#include "core/render/FontResolver.h"
#include "ui/Fonts.h"

#include "ui/Settings.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#if MICRONOTES_HAS_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace micronotes::ui {
namespace {

// The type scale as the user asked for it. Every size in it moves together, so
// a bigger body size does not leave the headings behind - the ratio between
// them is the design, and the multiplier only says how far away you are
// sitting. The line-height ratio is a proportion, so it stays put.
const TypeScale& typeScale() {
  static const TypeScale base {};
  static TypeScale scaled = base;
  static float applied = 1.0f;
  const float scale = textScale();
  if(std::abs(scale - applied) > 0.001f) {
    applied = scale;
    scaled = base;
    scaled.pageTitle *= scale;
    scaled.h1 *= scale;
    scaled.h2 *= scale;
    scaled.h3 *= scale;
    scaled.h4 *= scale;
    scaled.body *= scale;
    scaled.ui *= scale;
    scaled.small *= scale;
    scaled.tiny *= scale;
    scaled.mono *= scale;
  }
  return scaled;
}

// Vendored faces, relative to a font root. Order matters only for diagnostics.
constexpr const char* kSansRegular = "inter/Inter-Regular.otf";
constexpr const char* kSansItalic = "inter/Inter-Italic.otf";
constexpr const char* kSansStrong = "inter/Inter-SemiBold.otf";
constexpr const char* kSansStrongItalic = "inter/Inter-SemiBoldItalic.otf";
constexpr const char* kMonoRegular = "jetbrains-mono/JetBrainsMono-Regular.ttf";
constexpr const char* kMonoItalic = "jetbrains-mono/JetBrainsMono-Italic.ttf";
constexpr const char* kMonoStrong = "jetbrains-mono/JetBrainsMono-Bold.ttf";

// Used when the vendored files are missing. Any of these keeps the app running.
const std::vector<std::string>& systemFallbacks(bool mono, bool strong, bool italic) {
  static const std::vector<std::string> sansRegular {
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  static const std::vector<std::string> sansItalic {
    "/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
  };
  static const std::vector<std::string> sansStrong {
    "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
  };
  static const std::vector<std::string> sansStrongItalic {
    "/usr/share/fonts/truetype/noto/NotoSans-BoldItalic.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
  };
  static const std::vector<std::string> monoRegular {
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
  };
  static const std::vector<std::string> monoItalic {
    "/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf",
  };
  static const std::vector<std::string> monoStrong {
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
  };
  if(mono) return strong ? monoStrong : (italic ? monoItalic : monoRegular);
  if(strong) return italic ? sansStrongItalic : sansStrong;
  return italic ? sansItalic : sansRegular;
}

const std::vector<std::string>& emojiFallbacks() {
  static const std::vector<std::string> paths {
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
  };
  return paths;
}

// Candidate roots holding the vendored font tree, most specific first.
std::vector<std::filesystem::path> fontRoots() {
  std::vector<std::filesystem::path> roots;
  if(const char* fontDirEnv = std::getenv("MICRONOTES_FONT_DIR"); fontDirEnv && *fontDirEnv) {
    roots.emplace_back(fontDirEnv);
  }
  if(const char* base = SDL_GetBasePath(); base) {
    const std::filesystem::path exeDir(base);
    roots.push_back(exeDir / ".." / "share" / "micronotes" / "fonts");
    roots.push_back(exeDir / "fonts");
  }
#ifdef MICRONOTES_SOURCE_FONT_DIR
  roots.emplace_back(MICRONOTES_SOURCE_FONT_DIR);
#endif
  roots.emplace_back("/usr/local/share/micronotes/fonts");
  roots.emplace_back("/usr/share/micronotes/fonts");
  return roots;
}

}

const TypeScale& type() {
  return typeScale();
}

float headingSize(int level) {
  const auto& t = typeScale();
  switch(level) {
    case 1: return t.h1;
    case 2: return t.h2;
    case 3: return t.h3;
    default: return t.h4;
  }
}

#if MICRONOTES_HAS_SDL3_TTF

struct FontStore::Impl {
  bool ttfReady = false;
  float scale = 1.0f;
  std::filesystem::path root;
  std::string source = "system fonts";
  // Keyed by (family, strong, italic, pixel size).
  std::map<std::tuple<int, bool, bool, int>, TTF_Font*> cache;
  // TTF_AddFallbackFont does not take ownership, so the emoji faces are held
  // here and closed with the fonts that reference them.
  std::vector<TTF_Font*> fallbacks;
  // The colour emoji face, kept apart from the text fallbacks and opened once.
  // See `attachEmoji` for why it cannot be one of them.
  TTF_Font* iconFont = nullptr;
  bool iconTried = false;

  std::string facePath(bool mono, bool strong, bool italic) const {
    if(!root.empty()) {
      const char* relative = mono
        ? (strong ? kMonoStrong : (italic ? kMonoItalic : kMonoRegular))
        : (strong ? (italic ? kSansStrongItalic : kSansStrong) : (italic ? kSansItalic : kSansRegular));
      const auto candidate = root / relative;
      std::error_code ec;
      if(std::filesystem::exists(candidate, ec)) return candidate.string();
    }
    // Ask fontconfig what this system actually uses before falling back to
    // guessed paths: a machine without the hardcoded files rendered no text at
    // all, and pinning a face means never picking up the configured UI font.
    if(const auto resolved = render::resolveFontFile({mono, strong, italic}); !resolved.empty()) {
      return resolved;
    }
    for(const auto& path : systemFallbacks(mono, strong, italic)) {
      std::error_code ec;
      if(std::filesystem::exists(path, ec)) return path;
    }
    return {};
  }

  TTF_Font* open(bool mono, bool strong, bool italic, int px) {
    const auto key = std::make_tuple(mono ? 1 : 0, strong, italic, px);
    if(const auto found = cache.find(key); found != cache.end()) return found->second;

    const auto path = facePath(mono, strong, italic);
    TTF_Font* font = path.empty() ? nullptr : TTF_OpenFont(path.c_str(), static_cast<float>(px));
    if(!font && !(strong || italic)) {
      cache.emplace(key, nullptr);
      return nullptr;
    }
    // A missing styled face falls back to the regular one rather than to nothing.
    if(!font) font = open(mono, false, false, px);
    if(font) attachEmoji(font, px);
    cache.emplace(key, font);
    return font;
  }

  // A colour emoji font carries a single fixed bitmap strike - 128 pixels for
  // Noto - and SDL_ttf cannot scale it: ask for 14 and every glyph still comes
  // back at 128. Attached as a text fallback it would paint one emoji straight
  // over the four lines around it, so only a face that actually honours the
  // size asked for is attached here. The colour face is still opened, once, for
  // the icon path, which scales the rendered glyph itself.
  void attachEmoji(TTF_Font* font, int px) {
    for(const auto& emoji : emojiFallbacks()) {
      std::error_code ec;
      if(!std::filesystem::exists(emoji, ec)) continue;
      TTF_Font* fallback = TTF_OpenFont(emoji.c_str(), static_cast<float>(px));
      if(!fallback) continue;
      if(TTF_GetFontHeight(fallback) > px * 2) {
        TTF_CloseFont(fallback);
        continue;
      }
      TTF_AddFallbackFont(font, fallback);
      fallbacks.push_back(fallback);
      return;
    }
  }

  TTF_Font* icon() {
    if(iconTried) return iconFont;
    iconTried = true;
    for(const auto& emoji : emojiFallbacks()) {
      std::error_code ec;
      if(!std::filesystem::exists(emoji, ec)) continue;
      // Any size: an unscalable face ignores it, and a scalable one is asked
      // for something big enough to look right when scaled down.
      iconFont = TTF_OpenFont(emoji.c_str(), 48.0f);
      if(iconFont) return iconFont;
    }
    return nullptr;
  }

  void clear() {
    for(auto& [_, font] : cache) {
      if(font) TTF_CloseFont(font);
    }
    cache.clear();
    // After the fonts that referenced them, never before.
    for(TTF_Font* fallback : fallbacks) TTF_CloseFont(fallback);
    fallbacks.clear();
    // The icon face is referenced by nothing and survives a rescale: it is
    // rendered at its own size and scaled by the caller either way.
  }

  int pixelSize(const TextStyle& style) const {
    const float logical = style.size > 0.0f ? style.size : typeScale().body;
    return std::max(6, static_cast<int>(std::lround(logical * scale)));
  }

  TTF_Font* get(const TextStyle& style) {
    return open(style.family == FontFamily::Mono, style.strong, style.italic, pixelSize(style));
  }
};

FontStore::~FontStore() {
  shutdown();
}

bool FontStore::init() {
  if(impl_) return impl_->ttfReady;
  impl_ = new Impl();
  impl_->ttfReady = TTF_Init();
  if(!impl_->ttfReady) return false;
  for(const auto& candidate : fontRoots()) {
    std::error_code ec;
    if(!std::filesystem::exists(candidate / kSansRegular, ec)) continue;
    impl_->root = std::filesystem::weakly_canonical(candidate, ec);
    if(ec) impl_->root = candidate;
    impl_->source = impl_->root.string();
    break;
  }
  return true;
}

void FontStore::shutdown() {
  if(!impl_) return;
  impl_->clear();
  if(impl_->iconFont) TTF_CloseFont(impl_->iconFont);
  impl_->iconFont = nullptr;
  if(impl_->ttfReady) TTF_Quit();
  delete impl_;
  impl_ = nullptr;
}

bool FontStore::ready() const {
  return impl_ && impl_->ttfReady;
}

void FontStore::setDisplayScale(float scale) {
  if(!impl_) return;
  const float clamped = std::clamp(scale, 0.5f, 6.0f);
  if(std::abs(clamped - impl_->scale) < 0.01f) return;
  impl_->scale = clamped;
  impl_->clear();
}

float FontStore::displayScale() const {
  return impl_ ? impl_->scale : 1.0f;
}

bool FontStore::measure(std::string_view value, const TextStyle& style, int* width, int* height) const {
  if(!ready()) return false;
  TTF_Font* font = impl_->get(style);
  if(!font) return false;
  int w = 0;
  int h = 0;
  if(!TTF_GetStringSize(font, value.data(), value.size(), &w, &h)) return false;
  if(width) *width = w;
  if(height) *height = h;
  return true;
}

SDL_Surface* FontStore::render(std::string_view value, const TextStyle& style, SDL_Color color) const {
  if(!ready()) return nullptr;
  TTF_Font* font = impl_->get(style);
  if(!font) return nullptr;
  return TTF_RenderText_Blended(font, value.data(), value.size(), color);
}

int FontStore::lineHeight(const TextStyle& style) const {
  if(!ready()) return static_cast<int>(std::lround((style.size > 0.0f ? style.size : typeScale().body) * displayScale() * 1.4f));
  TTF_Font* font = impl_->get(style);
  if(!font) return static_cast<int>(std::lround((style.size > 0.0f ? style.size : typeScale().body) * displayScale() * 1.4f));
  return TTF_GetFontHeight(font);
}

bool FontStore::hasIconFont() const {
  return impl_ && impl_->ttfReady && impl_->icon() != nullptr;
}

SDL_Surface* FontStore::renderIcon(std::string_view value, SDL_Color color) const {
  if(!impl_ || !impl_->ttfReady || value.empty()) return nullptr;
  TTF_Font* font = impl_->icon();
  if(!font) return nullptr;
  return TTF_RenderText_Blended(font, value.data(), value.size(), color);
}

const char* FontStore::sourceDescription() const {
  return impl_ ? impl_->source.c_str() : "uninitialized";
}

#else

struct FontStore::Impl {
  float scale = 1.0f;
};

FontStore::~FontStore() { shutdown(); }
bool FontStore::init() { if(!impl_) impl_ = new Impl(); return false; }
void FontStore::shutdown() { delete impl_; impl_ = nullptr; }
bool FontStore::ready() const { return false; }
void FontStore::setDisplayScale(float scale) { if(impl_) impl_->scale = scale; }
float FontStore::displayScale() const { return impl_ ? impl_->scale : 1.0f; }
bool FontStore::measure(std::string_view, const TextStyle&, int*, int*) const { return false; }
SDL_Surface* FontStore::render(std::string_view, const TextStyle&, SDL_Color) const { return nullptr; }
int FontStore::lineHeight(const TextStyle& style) const {
  return static_cast<int>((style.size > 0.0f ? style.size : type().body) * displayScale() * 1.4f);
}
bool FontStore::hasIconFont() const { return false; }
SDL_Surface* FontStore::renderIcon(std::string_view, SDL_Color) const { return nullptr; }

const char* FontStore::sourceDescription() const { return "SDL3_ttf unavailable"; }

#endif

}
