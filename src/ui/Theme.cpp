#include "ui/Theme.h"

#include <cctype>
#include <string>

namespace micronotes::ui {
namespace {

constexpr SDL_Color rgb(unsigned value, Uint8 alpha = 255) {
  return SDL_Color {
    static_cast<Uint8>((value >> 16) & 0xff),
    static_cast<Uint8>((value >> 8) & 0xff),
    static_cast<Uint8>(value & 0xff),
    alpha,
  };
}

// `weight` of `color` over `onto`.
constexpr SDL_Color blend(SDL_Color color, SDL_Color onto, float weight) {
  const auto channel = [weight](Uint8 a, Uint8 b) {
    return static_cast<Uint8>(static_cast<float>(b) + (static_cast<float>(a) - static_cast<float>(b)) * weight);
  };
  return SDL_Color {channel(color.r, onto.r), channel(color.g, onto.g), channel(color.b, onto.b), 255};
}

Theme makeDark() {
  Theme t;
  t.mode = ThemeMode::Dark;

  t.text = rgb(0xD4D4D4);
  t.muted = rgb(0x9B9B9B);
  t.dim = rgb(0x6F6F6F);
  t.onAccent = rgb(0xFFFFFF);

  t.accent = rgb(0x529CCA);
  t.accentDim = rgb(0x2F5B78);
  t.accentSoft = rgb(0x1B2B36);
  t.warn = rgb(0xE07B5F);

  t.appBg = rgb(0x191919);
  t.sidebarBg = rgb(0x202020);
  t.notesBg = rgb(0x1D1D1D);
  t.editorBg = rgb(0x191919);
  t.viewerBg = rgb(0x191919);
  t.statusBg = rgb(0x1B1B1B);
  t.pageSurface = rgb(0x191919);
  t.surface = rgb(0x252525);
  t.surfaceElevated = rgb(0x2C2C2C);
  t.surfaceSheen = rgb(0x3A3A3A, 120);
  t.inputBg = rgb(0x252525);
  t.codeBg = rgb(0x1F1F1F);
  t.calloutBg = rgb(0x202B31);
  t.chipBg = rgb(0x2A2A2A);
  t.tableHeaderBg = rgb(0x242424);
  t.tableCellBg = rgb(0x1C1C1C);

  t.divider = rgb(0x373737);
  t.hairline = rgb(0x2A2A2A);

  t.hoverBg = rgb(0x2C2C2C);
  t.selectedBg = rgb(0x323232);
  t.selectionBg = rgb(0x2E4A63, 210);
  t.findBg = rgb(0x5B4D23, 230);
  t.findBorder = rgb(0xAA8E38, 220);

  t.scrollTrack = rgb(0x252525, 210);
  t.scrollThumb = rgb(0x4A4A4A);
  t.scrollThumbBorder = rgb(0x5A5A5A, 180);
  return t;
}

Theme makeLight() {
  Theme t;
  t.mode = ThemeMode::Light;

  t.text = rgb(0x37352F);
  t.muted = rgb(0x787774);
  t.dim = rgb(0x9B9A97);
  t.onAccent = rgb(0xFFFFFF);

  t.accent = rgb(0x2383E2);
  t.accentDim = rgb(0x9CC7EE);
  t.accentSoft = rgb(0xE7F3F8);
  t.warn = rgb(0xD44C47);

  t.appBg = rgb(0xFFFFFF);
  t.sidebarBg = rgb(0xF7F6F3);
  t.notesBg = rgb(0xFBFBFA);
  t.editorBg = rgb(0xFFFFFF);
  t.viewerBg = rgb(0xFFFFFF);
  t.statusBg = rgb(0xF7F6F3);
  t.pageSurface = rgb(0xFFFFFF);
  t.surface = rgb(0xFFFFFF);
  t.surfaceElevated = rgb(0xFFFFFF);
  t.surfaceSheen = rgb(0xFFFFFF, 0);
  t.inputBg = rgb(0xFFFFFF);
  t.codeBg = rgb(0xF7F6F3);
  t.calloutBg = rgb(0xF1F5F9);
  t.chipBg = rgb(0xEFEFEE);
  t.tableHeaderBg = rgb(0xF7F6F3);
  t.tableCellBg = rgb(0xFFFFFF);

  t.divider = rgb(0xDFDEDB);
  t.hairline = rgb(0xE9E9E7);

  t.hoverBg = rgb(0xEFEFEE);
  t.selectedBg = rgb(0xE7F3F8);
  t.selectionBg = rgb(0xACD5F0, 210);
  t.findBg = rgb(0xFDECC8, 240);
  t.findBorder = rgb(0xE0B65B, 220);

  t.scrollTrack = rgb(0xEDEDEC, 210);
  t.scrollThumb = rgb(0xC4C3C0);
  t.scrollThumbBorder = rgb(0xB0AFAC, 180);
  return t;
}

const Theme& darkTheme() {
  static const Theme value = makeDark();
  return value;
}

const Theme& lightTheme() {
  static const Theme value = makeLight();
  return value;
}

ThemeMode& activeMode() {
  static ThemeMode mode = ThemeMode::Dark;
  return mode;
}

}

CalloutStyle calloutStyle(std::string_view rawKind) {
  const bool light = activeMode() == ThemeMode::Light;
  // The live surface reads the tag as written and the reading view lowercases
  // it on the way through md4c; both must land on the same colour.
  std::string kind;
  for(char c : rawKind) kind.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  // GitHub's five alert kinds, which is what `> [!NOTE]` already means
  // everywhere else these files are read.
  unsigned hue = light ? 0x2383E2 : 0x529CCA;
  if(kind == "TIP") hue = light ? 0x0F7B6C : 0x4DAB9A;
  else if(kind == "IMPORTANT") hue = light ? 0x6940A5 : 0x9A6DD7;
  else if(kind == "WARNING") hue = light ? 0xCB912F : 0xE0A030;
  else if(kind == "CAUTION") hue = light ? 0xD44C47 : 0xE07B5F;

  CalloutStyle style;
  style.accent = rgb(hue);
  // The tint is the accent laid over the page rather than a second constant,
  // so a palette change carries it along.
  const SDL_Color page = light ? lightTheme().pageSurface : darkTheme().pageSurface;
  style.surface = blend(style.accent, page, light ? 0.09f : 0.14f);
  return style;
}

const Theme& theme() {
  return activeMode() == ThemeMode::Light ? lightTheme() : darkTheme();
}

ThemeMode themeMode() {
  return activeMode();
}

void setThemeMode(ThemeMode mode) {
  activeMode() = mode;
}

std::string_view themeModeName(ThemeMode mode) {
  return mode == ThemeMode::Light ? "light" : "dark";
}

ThemeMode themeModeFromName(std::string_view name) {
  return name == "light" ? ThemeMode::Light : ThemeMode::Dark;
}

}
