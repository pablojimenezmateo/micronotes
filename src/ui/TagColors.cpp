#include "CoreAliases.h"
#include "ui/TagColors.h"

#include "core/render/ColorMath.h"
#include "ui/Theme.h"

#include <algorithm>
#include <utility>

namespace micronotes::ui {
namespace {

constexpr SDL_Color rgb(unsigned value) {
  return SDL_Color {
    static_cast<Uint8>((value >> 16) & 0xff),
    static_cast<Uint8>((value >> 8) & 0xff),
    static_cast<Uint8>(value & 0xff),
    255,
  };
}

// Twelve hues, twice: light-and-saturated for a dark panel, deep for a light
// one. Not generated from a wheel -- an even sweep of hue puts three greens
// next to each other and skips the browns, so tags that ought to be told apart
// end up neighbours. These are picked, and picked out of the palette family the
// rest of the shell already draws from: the accent, the five callout kinds, the
// diff colours and the syntax hues. A tag's colour looks like it belongs to
// this application rather than to a colour picker.
//
// The first is the accent itself, because the first tag anybody colours should
// come out looking deliberate.
constexpr unsigned kDarkSwatches[kTagSwatchCount] = {
  0x729ED8,  // blue -- the accent
  0x8ED4E6,  // teal
  0x8DDF9F,  // green
  0xB5D889,  // lime
  0xE4B77B,  // amber
  0xEFA267,  // orange
  0xCD7A82,  // red
  0xFF7AA2,  // pink
  0xCBA6FF,  // violet
  0x9AA8F5,  // indigo
  0xC2995F,  // brown
  0xA8B4C5,  // slate
};

constexpr unsigned kLightSwatches[kTagSwatchCount] = {
  0x2F6FD6,
  0x1D7F95,
  0x2E8943,
  0x5B7F1E,
  0xA86911,
  0xB4641E,
  0xC3414B,
  0xBF365D,
  0x7B4CC3,
  0x4B57C0,
  0x8A6220,
  0x525C6B,
};

}

SDL_Color tagSwatch(int index) {
  // Wrapped into range, and correctly for a negative: `%` on a negative left
  // operand is negative in C++, which would index off the front of the array.
  const int count = kTagSwatchCount;
  const int wrapped = ((index % count) + count) % count;
  const bool light = themeMode() == ThemeMode::Light;
  const SDL_Color picked = rgb(light ? kLightSwatches[wrapped] : kDarkSwatches[wrapped]);
  // Held to the incidental ratio against the panel the dots are drawn on, for
  // the same reason the theme holds its own inks there: a swatch that arrives
  // from a palette nobody re-checked must not be able to draw an invisible dot.
  // A no-op on both built-in sets.
  return render::ensureContrast(picked, theme().surfaceBackground, render::kIncidentalContrast);
}

int defaultTagSwatch(std::string_view tag) {
  // FNV-1a over the bytes, byte at a time, written out here rather than taken
  // from `util::hashBytes`. That one is explicitly free to change because it
  // only keys in-memory caches; this one decides what colour a reader's tags
  // are, so changing it would repaint a whole library for no reason anybody
  // could see. It is pinned by a test for that reason.
  std::uint64_t hash = 1469598103934665603ull;
  for(const unsigned char byte : tag) {
    hash = (hash ^ byte) * 1099511628211ull;
  }
  return static_cast<int>(hash % static_cast<std::uint64_t>(kTagSwatchCount));
}

int TagColors::swatchOf(std::string_view tag) const {
  const auto found = picked_.find(tag);
  return found != picked_.end() ? found->second : defaultTagSwatch(tag);
}

bool TagColors::picked(std::string_view tag) const {
  return picked_.find(tag) != picked_.end();
}

void TagColors::set(std::string tag, int swatch) {
  if(tag.empty()) return;
  // Stored wrapped, so the file never carries an index the palette has no
  // swatch for and every reader of it agrees with `tagSwatch`.
  const int count = kTagSwatchCount;
  picked_[std::move(tag)] = ((swatch % count) + count) % count;
}

void TagColors::clear(std::string_view tag) {
  const auto found = picked_.find(tag);
  if(found != picked_.end()) picked_.erase(found);
}

void TagColors::clearAll() {
  picked_.clear();
}

const std::map<std::string, int, std::less<>>& TagColors::choices() const {
  return picked_;
}

SDL_Color tagColor(const TagColors& colors, std::string_view tag) {
  return tagSwatch(colors.swatchOf(tag));
}

}
