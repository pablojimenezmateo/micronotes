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

// A colour as it would be written in the palette, for a failure message that
// can be compared against the source by eye.
std::string hex(unsigned packed) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out = "0x";
  for(int shift = 20; shift >= 0; shift -= 4) out.push_back(kDigits[(packed >> shift) & 0xF]);
  return out;
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
    {"body text on the page", &micronotes::ui::Theme::textPrimary, &micronotes::ui::Theme::editorBackground, 4.5f},
    {"body text on a panel", &micronotes::ui::Theme::textPrimary, &micronotes::ui::Theme::surfaceBackground, 4.5f},
    {"secondary text in the sidebar", &micronotes::ui::Theme::textSecondary, &micronotes::ui::Theme::surfaceBackground, 4.5f},
    {"a tree row's label on the highlight", &micronotes::ui::Theme::textSecondary, &micronotes::ui::Theme::rowHighlight, 4.5f},
    {"section labels in the sidebar", &micronotes::ui::Theme::textMuted, &micronotes::ui::Theme::surfaceBackground, 3.0f},
    {"status bar text", &micronotes::ui::Theme::chromeText, &micronotes::ui::Theme::chromeBackground, 4.5f},
    {"the active tab's label", &micronotes::ui::Theme::chromeActiveText, &micronotes::ui::Theme::chromeActive, 4.5f},
    {"an inactive tab's glyphs", &micronotes::ui::Theme::chromeTextSecondary, &micronotes::ui::Theme::chromeBackground, 3.0f},
    {"links on the page", &micronotes::ui::Theme::accent, &micronotes::ui::Theme::editorBackground, 3.0f},
    {"links to a note not written yet", &micronotes::ui::Theme::linkPending, &micronotes::ui::Theme::editorBackground, 3.0f},
    {"text on an accent fill", &micronotes::ui::Theme::onAccent, &micronotes::ui::Theme::accent, 4.5f},
    {"code on its own background", &micronotes::ui::Theme::textPrimary, &micronotes::ui::Theme::codeBackground, 4.5f},
    {"menu text", &micronotes::ui::Theme::textPrimary, &micronotes::ui::Theme::overlayBackground, 4.5f},
    {"the caret against the page", &micronotes::ui::Theme::cursor, &micronotes::ui::Theme::editorBackground, 3.0f},
  };

  // The grounds, which a flat interface leans on as heavily as it leans on the
  // ink: with no shadow and no radius to separate them, two panels that differ
  // by less than this read as one panel and the layout stops being legible.
  struct Separation {
    const char* what;
    SDL_Color micronotes::ui::Theme::*surface;
    SDL_Color micronotes::ui::Theme::*reference;
  };
  static constexpr Separation kSeparations[] = {
    {"the chrome against the page it frames", &micronotes::ui::Theme::chromeBackground,
     &micronotes::ui::Theme::editorBackground},
    {"the active tab against its strip", &micronotes::ui::Theme::chromeActive,
     &micronotes::ui::Theme::chromeBackground},
    {"a raised control against its panel", &micronotes::ui::Theme::surfaceRaised,
     &micronotes::ui::Theme::surfaceBackground},
    {"a highlighted row against its panel", &micronotes::ui::Theme::rowHighlight,
     &micronotes::ui::Theme::surfaceBackground},
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
    for(const auto& separation : kSeparations) {
      const float ratio = contrast(palette.*(separation.surface), palette.*(separation.reference));
      micronotes::tests::require(
        ratio >= micronotes::ui::kSurfaceSeparation - 0.005f,
        which + " theme: " + separation.what + " differs by " + std::to_string(ratio) +
          ":1, under the " + std::to_string(micronotes::ui::kSurfaceSeparation) +
          ":1 two grounds need to read as two");
      micronotes::tests::require(
        micronotes::ui::samePolarity(palette.*(separation.surface), palette.*(separation.reference)),
        std::string(which) + " theme: " + separation.what +
          " crosses the light/dark threshold -- a panel that is a raised card in one theme and a "
          "hole in the other is a palette nobody looked at");
    }
  }
}

// The built-in palettes reach the screen as they were written.
//
// The corrector used to run over them, and its ground pass rewrote four roles
// that somebody had chosen: the chrome came out 0x31353D rather than the
// 0x151922 in `makeDark`, because that pairing sits 1.057 off the page and the
// floor was written as 1.08. A palette that arrives from a theme file still
// gets corrected -- it has grounds nobody vetted -- but a value in this tree is
// a decision, and this is what says so.
//
// Spelled out as literals on purpose. Comparing the corrected palette against
// `makeDark()` would pass just as happily if both drifted, and the point of the
// test is that these exact bytes -- the sibling microide's, so the two
// applications look like one family -- survive the trip.
MICRONOTES_TEST(theme_built_in_grounds_reach_the_screen_as_written) {
  struct Ground {
    const char* what;
    SDL_Color micronotes::ui::Theme::*role;
    unsigned dark;
    unsigned light;
  };
  static constexpr Ground kGrounds[] = {
    {"windowBackground", &micronotes::ui::Theme::windowBackground, 0x080B11, 0xE7EAF0},
    {"chromeBackground", &micronotes::ui::Theme::chromeBackground, 0x151922, 0xDCE1E9},
    {"chromeActive", &micronotes::ui::Theme::chromeActive, 0x1D2431, 0xCCD4E0},
    {"surfaceBackground", &micronotes::ui::Theme::surfaceBackground, 0x121722, 0xF3F5F9},
    {"surfaceRaised", &micronotes::ui::Theme::surfaceRaised, 0x1A2130, 0xFFFFFF},
    {"editorBackground", &micronotes::ui::Theme::editorBackground, 0x0F131B, 0xFBFCFE},
    {"gutterBackground", &micronotes::ui::Theme::gutterBackground, 0x0C1017, 0xEFF2F6},
    {"rowHighlight", &micronotes::ui::Theme::rowHighlight, 0x171F2B, 0xE5EBF6},
    // The one rule weight the shell draws. Held to a text-ish ratio it came out
    // 0x333D50, which is a rule dark enough to turn a list of rows into a list
    // of boxes -- so it is picked, not corrected, like the grounds it separates.
    {"border", &micronotes::ui::Theme::border, 0x2A3548, 0xC3CBD7},
  };

  for(const auto mode : {micronotes::ui::ThemeMode::Dark, micronotes::ui::ThemeMode::Light}) {
    micronotes::ui::setThemeMode(mode);
    const auto& palette = micronotes::ui::theme();
    const bool dark = mode == micronotes::ui::ThemeMode::Dark;
    const std::string which = dark ? "dark" : "light";
    for(const auto& ground : kGrounds) {
      const unsigned want = dark ? ground.dark : ground.light;
      const SDL_Color got = palette.*(ground.role);
      const unsigned packed = (static_cast<unsigned>(got.r) << 16) |
                              (static_cast<unsigned>(got.g) << 8) | static_cast<unsigned>(got.b);
      micronotes::tests::require(
        packed == want,
        which + " theme: " + ground.what + " reaches the screen as " + hex(packed) +
          " but was written as " + hex(want) + " -- something is correcting a picked ground");
      micronotes::tests::require(got.a == 255,
                                 which + " theme: " + ground.what + " lost its opacity");
    }
  }
  micronotes::ui::setThemeMode(micronotes::ui::ThemeMode::Dark);
}
