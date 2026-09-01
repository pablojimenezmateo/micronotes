#include "TestSupport.h"

#include "ui/ColorMath.h"
#include "ui/Theme.h"

#include <string>

using micronotes::ui::blend;
using micronotes::ui::compositeOver;
using micronotes::ui::contrast;
using micronotes::ui::ensureContrast;
using micronotes::ui::isLight;
using micronotes::ui::relativeLuminance;

namespace {

constexpr SDL_Color kBlack {0, 0, 0, 255};
constexpr SDL_Color kWhite {255, 255, 255, 255};

bool near(float a, float b, float tolerance = 0.01f) {
  return (a > b ? a - b : b - a) <= tolerance;
}

// A small deterministic sequence, so "lots of pairs" is reproducible.
Uint8 nextByte(unsigned& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<Uint8>((state >> 16) & 0xFF);
}

}

MICRONOTES_TEST(color_contrast_matches_the_wcag_extremes) {
  MICRONOTES_REQUIRE(near(contrast(kBlack, kWhite), 21.0f));
  MICRONOTES_REQUIRE(near(contrast(kWhite, kBlack), 21.0f));
  MICRONOTES_REQUIRE(near(contrast(kWhite, kWhite), 1.0f));
  MICRONOTES_REQUIRE(near(relativeLuminance(kWhite), 1.0f));
  MICRONOTES_REQUIRE(near(relativeLuminance(kBlack), 0.0f));
}

MICRONOTES_TEST(color_blend_reaches_both_ends_and_keeps_alpha) {
  const SDL_Color base {10, 20, 30, 128};
  MICRONOTES_REQUIRE(blend(base, kWhite, 0.0f).r == 10);
  MICRONOTES_REQUIRE(blend(base, kWhite, 1.0f).r == 255);
  // Out-of-range amounts clamp rather than overshoot into nonsense.
  MICRONOTES_REQUIRE(blend(base, kWhite, -5.0f).r == 10);
  MICRONOTES_REQUIRE(blend(base, kWhite, 5.0f).g == 255);
  MICRONOTES_REQUIRE(blend(base, kWhite, 0.5f).a == 128);
}

// What a translucent fill will actually look like once it is drawn.
MICRONOTES_TEST(color_composite_over_resolves_translucency) {
  const SDL_Color half {255, 255, 255, 128};
  const SDL_Color result = compositeOver(half, kBlack);
  MICRONOTES_REQUIRE(result.a == 255);
  MICRONOTES_REQUIRE(result.r > 120 && result.r < 136);
  // Fully opaque and fully transparent are the two ends.
  MICRONOTES_REQUIRE(compositeOver({1, 2, 3, 255}, kWhite).r == 1);
  MICRONOTES_REQUIRE(compositeOver({1, 2, 3, 0}, kWhite).r == 255);
}

MICRONOTES_TEST(color_is_light_splits_where_black_and_white_agree) {
  MICRONOTES_REQUIRE(isLight(kWhite));
  MICRONOTES_REQUIRE(!isLight(kBlack));
  // At the threshold, black and white text contrast equally against it.
  const SDL_Color mid {119, 119, 119, 255};
  MICRONOTES_REQUIRE(near(contrast(mid, kWhite), contrast(mid, kBlack), 0.35f));
}

MICRONOTES_TEST(color_ensure_contrast_leaves_a_passing_pair_alone) {
  const SDL_Color result = ensureContrast(kBlack, kWhite, 4.5f);
  MICRONOTES_REQUIRE(result.r == kBlack.r && result.g == kBlack.g && result.b == kBlack.b);
}

// The property that matters: whatever goes in, what comes out either meets the
// ratio or is the best that colour could manage -- and is never worse than what
// went in, never NaN, and never a different alpha.
MICRONOTES_TEST(color_ensure_contrast_never_makes_a_pair_worse) {
  unsigned state = 12345u;
  for(int i = 0; i < 400; ++i) {
    const SDL_Color foreground {nextByte(state), nextByte(state), nextByte(state), nextByte(state)};
    const SDL_Color background {nextByte(state), nextByte(state), nextByte(state), 255};
    const SDL_Color fixed = ensureContrast(foreground, background, 4.5f);
    const float before = contrast(foreground, background);
    const float after = contrast(fixed, background);
    micronotes::tests::require(after == after, "ensureContrast produced a NaN ratio");
    micronotes::tests::require(after >= before - 0.001f,
                               "ensureContrast lowered the contrast it was asked to raise");
    micronotes::tests::require(fixed.a == foreground.a, "ensureContrast changed the alpha");
  }
}

// The ratchet. Every pairing a reader has to make out is checked in both
// themes, so a palette change that makes text illegible fails here rather than
// shipping. The light theme's `dim` on `sidebarBg` was 2.4:1 when this was
// written, which is what prompted it.
MICRONOTES_TEST(theme_palettes_stay_legible_in_both_modes) {
  const auto previous = micronotes::ui::themeMode();
  struct Restore {
    micronotes::ui::ThemeMode mode;
    ~Restore() { micronotes::ui::setThemeMode(mode); }
  } restore {previous};

  struct Pairing {
    const char* what;
    SDL_Color micronotes::ui::Theme::*foreground;
    SDL_Color micronotes::ui::Theme::*background;
    float minimum;
  };
  static constexpr Pairing kPairings[] = {
    {"body text on the page", &micronotes::ui::Theme::text, &micronotes::ui::Theme::pageSurface, 4.5f},
    {"body text on the editor", &micronotes::ui::Theme::text, &micronotes::ui::Theme::editorBg, 4.5f},
    {"secondary text in the sidebar", &micronotes::ui::Theme::muted, &micronotes::ui::Theme::sidebarBg, 4.5f},
    {"secondary text in the note list", &micronotes::ui::Theme::muted, &micronotes::ui::Theme::notesBg, 4.5f},
    {"section labels in the sidebar", &micronotes::ui::Theme::dim, &micronotes::ui::Theme::sidebarBg, 3.0f},
    {"status bar text", &micronotes::ui::Theme::muted, &micronotes::ui::Theme::statusBg, 4.5f},
    {"links on the page", &micronotes::ui::Theme::accent, &micronotes::ui::Theme::pageSurface, 3.0f},
    {"links to a note not written yet", &micronotes::ui::Theme::linkPending, &micronotes::ui::Theme::pageSurface, 3.0f},
    {"text on an accent fill", &micronotes::ui::Theme::onAccent, &micronotes::ui::Theme::accent, 4.5f},
    {"code on its own background", &micronotes::ui::Theme::text, &micronotes::ui::Theme::codeBg, 4.5f},
    {"menu text", &micronotes::ui::Theme::text, &micronotes::ui::Theme::surfaceElevated, 4.5f},
    {"input text", &micronotes::ui::Theme::text, &micronotes::ui::Theme::inputBg, 4.5f},
  };

  for(const auto mode : {micronotes::ui::ThemeMode::Light, micronotes::ui::ThemeMode::Dark}) {
    micronotes::ui::setThemeMode(mode);
    const auto& palette = micronotes::ui::theme();
    const std::string which = mode == micronotes::ui::ThemeMode::Light ? "light" : "dark";
    for(const auto& pairing : kPairings) {
      const float ratio = contrast(palette.*(pairing.foreground), palette.*(pairing.background));
      micronotes::tests::require(
        ratio >= pairing.minimum - 0.05f,
        which + " theme: " + pairing.what + " is " + std::to_string(ratio) +
          ":1, under the " + std::to_string(pairing.minimum) + ":1 this pairing is held to");
    }
  }
}
