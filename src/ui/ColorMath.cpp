#include "ui/ColorMath.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

float toLinear(Uint8 value) {
  const float v = static_cast<float>(value) / 255.0f;
  if(v <= 0.04045f) return v / 12.92f;
  return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

}

float relativeLuminance(SDL_Color color) {
  return 0.2126f * toLinear(color.r) + 0.7152f * toLinear(color.g) + 0.0722f * toLinear(color.b);
}

float contrast(SDL_Color a, SDL_Color b) {
  float high = relativeLuminance(a);
  float low = relativeLuminance(b);
  if(high < low) std::swap(high, low);
  return (high + 0.05f) / (low + 0.05f);
}

SDL_Color blend(SDL_Color base, SDL_Color tint, float amount) {
  const float t = std::clamp(amount, 0.0f, 1.0f);
  const auto mix = [t](Uint8 from, Uint8 to) {
    return static_cast<Uint8>(std::lround(static_cast<float>(from) * (1.0f - t) +
                                          static_cast<float>(to) * t));
  };
  return {mix(base.r, tint.r), mix(base.g, tint.g), mix(base.b, tint.b), base.a};
}

SDL_Color compositeOver(SDL_Color foreground, SDL_Color background) {
  const float alpha = static_cast<float>(foreground.a) / 255.0f;
  const auto over = [alpha](Uint8 fg, Uint8 bg) {
    return static_cast<Uint8>(std::lround(static_cast<float>(fg) * alpha +
                                          static_cast<float>(bg) * (1.0f - alpha)));
  };
  return {over(foreground.r, background.r), over(foreground.g, background.g),
          over(foreground.b, background.b), SDL_ALPHA_OPAQUE};
}

bool isLight(SDL_Color color) {
  // 0.179 is where white and black text contrast equally: sqrt(1.05 * 0.05) - 0.05.
  return relativeLuminance(color) >= 0.179f;
}

SDL_Color ensureContrast(SDL_Color foreground, SDL_Color background, float minimumContrast) {
  float best = contrast(foreground, background);
  if(best >= minimumContrast) return foreground;

  // Away from the background, not toward some fixed colour: darkening text on a
  // dark panel would make it worse.
  const SDL_Color target = isLight(background) ? SDL_Color {0, 0, 0, foreground.a}
                                               : SDL_Color {255, 255, 255, foreground.a};
  SDL_Color result = foreground;
  // Twenty-four steps: fine enough that the hue is recognisably the one asked
  // for, coarse enough to stop well short of a search.
  for(int step = 1; step <= 24; ++step) {
    SDL_Color candidate = blend(foreground, target, static_cast<float>(step) / 24.0f);
    candidate.a = foreground.a;
    const float ratio = contrast(candidate, background);
    if(ratio > best) {
      best = ratio;
      result = candidate;
    }
    if(ratio >= minimumContrast) return candidate;
  }
  // Unreachable ratios happen -- nothing meets 4.5 against mid grey. The best
  // available beats returning the original unchanged.
  return result;
}

}
