#include "ui/Theme.h"

#include "ui/ColorMath.h"

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

Theme makeDark() {
  Theme t;
  t.mode = ThemeMode::Dark;

  t.text = rgb(0xDADADA);
  t.muted = rgb(0x989898);
  t.dim = rgb(0x707070);
  t.onAccent = rgb(0xFFFFFF);

  // hsl(254, 80%, 68%). Obsidian's accent is one hue with the lightness moved
  // per role rather than five unrelated purples, so the dim and soft variants
  // below are that same hue darkened, not separate picks.
  t.accent = rgb(0x8B6CEF);
  // A link to a note that is not there yet stays in the accent's family and
  // steps back from it, so "not written yet" reads as quieter rather than as a
  // different kind of link.
  t.linkPending = rgb(0x6F5CB0);
  t.accentDim = rgb(0x4A3D7A);
  t.accentSoft = rgb(0x272141);
  t.warn = rgb(0xFB464C);

  // The page is the lightest thing on screen and the chrome steps away from it.
  // This is the reverse of the palette this replaced, where the sidebar was
  // lighter than the page it framed.
  t.appBg = rgb(0x1E1E1E);
  t.sidebarBg = rgb(0x161616);
  t.notesBg = rgb(0x1A1A1A);
  t.editorBg = rgb(0x1E1E1E);
  t.viewerBg = rgb(0x1E1E1E);
  t.statusBg = rgb(0x161616);
  t.pageSurface = rgb(0x1E1E1E);
  t.surface = rgb(0x262626);
  t.surfaceElevated = rgb(0x2A2A2A);
  // No sheen. A lit top edge is what makes a panel read as raised out of the
  // page; these panels are meant to read as cut into it.
  t.surfaceSheen = rgb(0xFFFFFF, 0);
  t.inputBg = rgb(0x1E1E1E);
  t.codeBg = rgb(0x171717);
  t.calloutBg = rgb(0x232132);
  t.chipBg = rgb(0x2A2A2A);
  t.tableHeaderBg = rgb(0x242424);
  t.tableCellBg = rgb(0x1E1E1E);

  t.divider = rgb(0x363636);
  t.hairline = rgb(0x2A2A2A);

  t.hoverBg = rgb(0x262626);
  t.selectedBg = rgb(0x2F2F2F);
  // Selected text is the accent laid over the page rather than a blue of its
  // own, so a palette change carries the selection with it.
  t.selectionBg = rgb(0x483A7C, 200);
  t.findBg = rgb(0x5B4D23, 230);
  t.findBorder = rgb(0xAA8E38, 220);

  t.scrollTrack = rgb(0x1E1E1E, 0);
  t.scrollThumb = rgb(0x3A3A3A);
  t.scrollThumbBorder = rgb(0x4A4A4A, 160);
  return t;
}

Theme makeLight() {
  Theme t;
  t.mode = ThemeMode::Light;

  t.text = rgb(0x222222);
  t.muted = rgb(0x5C5C5C);
  t.dim = rgb(0x8A8A8A);
  t.onAccent = rgb(0xFFFFFF);

  // The same hue as dark mode, darkened until it carries text on white. The
  // corrector below would have done it, but a palette that needs correcting to
  // be legible is one nobody has actually looked at.
  t.accent = rgb(0x6C4FD6);
  t.linkPending = rgb(0x8F7CC4);
  t.accentDim = rgb(0xC0B0F0);
  t.accentSoft = rgb(0xEFEBFC);
  t.warn = rgb(0xE93147);

  t.appBg = rgb(0xFFFFFF);
  t.sidebarBg = rgb(0xF6F6F6);
  t.notesBg = rgb(0xFAFAFA);
  t.editorBg = rgb(0xFFFFFF);
  t.viewerBg = rgb(0xFFFFFF);
  t.statusBg = rgb(0xF6F6F6);
  t.pageSurface = rgb(0xFFFFFF);
  t.surface = rgb(0xFFFFFF);
  t.surfaceElevated = rgb(0xFFFFFF);
  t.surfaceSheen = rgb(0xFFFFFF, 0);
  t.inputBg = rgb(0xFFFFFF);
  t.codeBg = rgb(0xF2F2F2);
  t.calloutBg = rgb(0xF0EDFB);
  t.chipBg = rgb(0xEDEDED);
  t.tableHeaderBg = rgb(0xF6F6F6);
  t.tableCellBg = rgb(0xFFFFFF);

  t.divider = rgb(0xD4D4D4);
  t.hairline = rgb(0xE3E3E3);

  t.hoverBg = rgb(0xEBEBEB);
  t.selectedBg = rgb(0xE4E4E4);
  t.selectionBg = rgb(0xCFC2F5, 210);
  t.findBg = rgb(0xFDECC8, 240);
  t.findBorder = rgb(0xE0B65B, 220);

  t.scrollTrack = rgb(0xFFFFFF, 0);
  t.scrollThumb = rgb(0xC9C9C9);
  t.scrollThumbBorder = rgb(0xB4B4B4, 160);
  return t;
}

// The palette as written, corrected so that nothing a reader has to make out
// falls below the ratio it is held to.
//
// Correcting here rather than editing hex values by hand is the point: the
// author picks the colour they mean, and the one pairing they did not think to
// check cannot ship illegible. It also means a palette that arrives from
// somewhere else -- a theme file -- gets the same guarantee as the built-ins.
//
// Only the text roles are moved. Surfaces, borders and fills are left exactly
// as chosen, because nudging a background to satisfy one pairing changes every
// other pairing that shares it.
Theme correctContrast(Theme t) {
  // Each text role against the darkest surface it is drawn on: fixing it there
  // fixes it everywhere, since every other surface has more contrast to give.
  const SDL_Color panels[] = {t.sidebarBg, t.notesBg, t.statusBg, t.pageSurface, t.editorBg};
  const auto worst = [&](SDL_Color foreground, float minimum) {
    SDL_Color result = foreground;
    for(const SDL_Color panel : panels) result = ensureContrast(result, panel, minimum);
    return result;
  };
  t.text = worst(t.text, kTextContrast);
  t.muted = worst(t.muted, kTextContrast);
  // Section labels, counts and markers are incidental: holding them to the body
  // ratio would flatten the whole hierarchy into one weight.
  t.dim = worst(t.dim, kIncidentalContrast);
  t.onAccent = ensureContrast(t.onAccent, t.accent, kTextContrast);
  t.accent = worst(t.accent, kIncidentalContrast);
  t.linkPending = worst(t.linkPending, kIncidentalContrast);
  t.warn = worst(t.warn, kIncidentalContrast);
  return t;
}

const Theme& darkTheme() {
  static const Theme value = correctContrast(makeDark());
  return value;
}

const Theme& lightTheme() {
  static const Theme value = correctContrast(makeLight());
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
  // everywhere else these files are read, in the hues Obsidian gives each of
  // them: note blue, tip cyan, important purple, warning orange, caution red.
  unsigned hue = light ? 0x086DDD : 0x027AFF;
  if(kind == "TIP") hue = light ? 0x00BFBC : 0x53DFDD;
  else if(kind == "IMPORTANT") hue = light ? 0x7852EE : 0xA882FF;
  else if(kind == "WARNING") hue = light ? 0xEC7500 : 0xE9973F;
  else if(kind == "CAUTION") hue = light ? 0xE93147 : 0xFB464C;

  CalloutStyle style;
  style.accent = rgb(hue);
  // The tint is the accent laid over the page rather than a second constant,
  // so a palette change carries it along.
  const SDL_Color page = light ? lightTheme().pageSurface : darkTheme().pageSurface;
  style.surface = blend(page, style.accent, light ? 0.09f : 0.14f);
  // The kind's name is drawn in the accent on that tint, so the accent has to
  // stay legible against the surface it just tinted.
  style.accent = ensureContrast(style.accent, style.surface, kIncidentalContrast);
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
