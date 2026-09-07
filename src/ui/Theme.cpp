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

// The palette proper: the colours somebody chose, before anything derived from
// them or corrected against them.
//
// Blue-slate rather than the neutral greys and purple accent this replaces, and
// the values are the sibling microide tree's verbatim, because the point of the
// exercise was that the two applications should look like the same family.
// `codeBackground`, `tableHeaderBackground` and `linkPending` are the three
// roles microide has no counterpart for, and they are derived below rather than
// picked, so a change to the grounds carries them along.
Theme makeDark() {
  Theme t;
  t.mode = ThemeMode::Dark;

  t.windowBackground = rgb(0x080B11);
  t.chromeBackground = rgb(0x151922);
  t.chromeActive = rgb(0x1D2431);
  t.chromeText = rgb(0xD7DEEA);
  t.chromeActiveText = rgb(0xF8FBFF);
  t.chromeTextSecondary = rgb(0xA8B4C5);

  t.surfaceBackground = rgb(0x121722);
  t.surfaceRaised = rgb(0x1A2130);
  t.surfaceText = rgb(0xD7DEEA);
  t.overlayBackground = rgb(0x1A2130, 0xF6);
  t.overlayBackdrop = rgb(0x05070C, 0xAA);

  t.editorBackground = rgb(0x0F131B);
  t.gutterBackground = rgb(0x0C1017);

  t.textPrimary = rgb(0xD7DEEA);
  t.textSecondary = rgb(0xAEB9C8);
  t.textMuted = rgb(0x8B98AB);
  t.textDisabled = rgb(0x657386);
  t.onAccent = rgb(0x080B11);

  t.accent = rgb(0x729ED8);
  t.warn = rgb(0xCD7A82);
  t.cursor = rgb(0xF8FBFF);

  t.border = rgb(0x2A3548);

  t.rowHighlight = rgb(0x171F2B);
  t.selectionFill = rgb(0x294F73, 0xA8);
  t.searchMatch = rgb(0x3A435D, 0x68);
  t.searchMatchActive = rgb(0x556891, 0x92);
  return t;
}

Theme makeLight() {
  Theme t;
  t.mode = ThemeMode::Light;

  t.windowBackground = rgb(0xE7EAF0);
  t.chromeBackground = rgb(0xDCE1E9);
  t.chromeActive = rgb(0xCCD4E0);
  t.chromeText = rgb(0x232934);
  t.chromeActiveText = rgb(0x0F141C);
  t.chromeTextSecondary = rgb(0x525C6B);

  t.surfaceBackground = rgb(0xF3F5F9);
  t.surfaceRaised = rgb(0xFFFFFF);
  t.surfaceText = rgb(0x232934);
  t.overlayBackground = rgb(0xFFFFFF, 0xF8);
  t.overlayBackdrop = rgb(0x374050, 0x44);

  t.editorBackground = rgb(0xFBFCFE);
  t.gutterBackground = rgb(0xEFF2F6);

  t.textPrimary = rgb(0x232934);
  t.textSecondary = rgb(0x495362);
  t.textMuted = rgb(0x6B7687);
  t.textDisabled = rgb(0x99A3B1);
  t.onAccent = rgb(0xFFFFFF);

  t.accent = rgb(0x2F6FD6);
  t.warn = rgb(0xC3414B);
  t.cursor = rgb(0x11161D);

  t.border = rgb(0xC3CBD7);

  t.rowHighlight = rgb(0xE5EBF6);
  t.selectionFill = rgb(0x4A90E2, 0x55);
  t.searchMatch = rgb(0xF2D96B, 0x82);
  t.searchMatchActive = rgb(0xEFBE48, 0xB2);
  return t;
}

// The roles nothing picks: each is a fixed step off a ground that was picked,
// so moving a ground moves them with it.
Theme derive(Theme t) {
  const bool light = t.mode == ThemeMode::Light;

  // Code sits one step *back* from the page in either palette -- darker on a
  // dark one, and on a light one the page is already near white, so back means
  // grey. The gutter is the same step, which is why code borrows it.
  t.codeBackground = t.gutterBackground;
  t.tableHeaderBackground = light ? darken(t.editorBackground, 0.05f)
                                  : lighten(t.editorBackground, 0.05f);
  // A link to a note that is not written yet: the accent, stepped back toward
  // the page it is drawn on, so "not there yet" reads as quieter rather than as
  // a second kind of link.
  t.linkPending = blend(t.accent, t.editorBackground, 0.45f);
  // Opaque, for the panels a translucent fill cannot be laid over: the sidebar
  // draws a selected snippet's match on top of a row fill that is itself on top
  // of the panel, and stacking two alphas there produced a third colour.
  t.selectionStrong = compositeOver(t.selectionFill, t.surfaceBackground);
  return t;
}

// The palette as written, corrected so that nothing a reader has to make out
// falls below the ratio it is held to, and no two grounds meant to read as
// separate end up the same.
//
// Correcting here rather than editing hex values by hand is the point: the
// author picks the colour they mean, and the one pairing they did not think to
// check cannot ship illegible. It also means a palette that arrives from
// somewhere else -- a theme file -- gets the same guarantee as the built-ins.
Theme correct(Theme t) {
  // Every text role against the darkest ground it is drawn on: fixing it there
  // fixes it everywhere, since every other ground has more contrast to give.
  const SDL_Color grounds[] = {
    t.windowBackground, t.chromeBackground, t.chromeActive, t.surfaceBackground,
    t.surfaceRaised, t.editorBackground, t.gutterBackground, t.codeBackground,
    t.rowHighlight,
  };
  const auto worst = [&](SDL_Color foreground, float minimum) {
    SDL_Color result = foreground;
    for(const SDL_Color ground : grounds) result = ensureContrast(result, ground, minimum);
    return result;
  };

  // The grounds first, because the text is corrected against them. A flat
  // interface has no shadow and no radius to say where one surface stops, so
  // two surfaces a fraction of a step apart are one surface.
  //
  // Only the *derived* separations are enforced: the chrome against the page it
  // frames, the active tab against the strip it sits in, and a raised control
  // against the panel under it. Nudging a picked ground would change every
  // other pairing that shares it.
  t.chromeBackground = ensureBackgroundSeparation(t.chromeBackground, t.editorBackground,
                                                  kSurfaceSeparation);
  t.chromeActive = ensureBackgroundSeparation(t.chromeActive, t.chromeBackground,
                                              kSurfaceSeparation);
  t.surfaceRaised = ensureBackgroundSeparation(t.surfaceRaised, t.surfaceBackground,
                                               kSurfaceSeparation);
  t.rowHighlight = ensureBackgroundSeparation(t.rowHighlight, t.surfaceBackground,
                                              kSurfaceSeparation);
  // A panel that reads as a raised card in one theme and as a hole in the other
  // is a palette nobody checked. Both are legitimate designs; mixing them is
  // not, and the polarity is what says which was meant.
  if(!samePolarity(t.surfaceRaised, t.surfaceBackground)) {
    t.surfaceRaised = t.mode == ThemeMode::Light ? darken(t.surfaceBackground, 0.06f)
                                                 : lighten(t.surfaceBackground, 0.06f);
  }
  // The border has to be visible against the panels it separates without
  // reading as text. Held to the separation ratio against the lightest ground
  // rather than to a text ratio, because a 1px rule at full contrast turns a
  // list of rows into a list of boxes.
  t.border = ensureContrast(t.border, t.surfaceBackground, 1.5f);

  t.textPrimary = worst(t.textPrimary, kTextContrast);
  t.textSecondary = worst(t.textSecondary, kTextContrast);
  t.surfaceText = worst(t.surfaceText, kTextContrast);
  t.chromeText = ensureContrast(t.chromeText, t.chromeBackground, kTextContrast);
  t.chromeActiveText = ensureContrast(t.chromeActiveText, t.chromeActive, kTextContrast);
  // Section labels, counts, markers and the strip's inactive glyphs are
  // incidental: holding them to the body ratio would flatten the whole
  // hierarchy into one weight.
  t.textMuted = worst(t.textMuted, kIncidentalContrast);
  t.chromeTextSecondary = ensureContrast(t.chromeTextSecondary, t.chromeBackground,
                                         kIncidentalContrast);
  // Not corrected: `textDisabled` is meant to be hard to read. A control that
  // will not answer should look like one.
  t.onAccent = ensureContrast(t.onAccent, t.accent, kTextContrast);
  t.accent = worst(t.accent, kIncidentalContrast);
  t.linkPending = worst(t.linkPending, kIncidentalContrast);
  t.warn = worst(t.warn, kIncidentalContrast);
  t.cursor = worst(t.cursor, kIncidentalContrast);
  return t;
}

const Theme& darkTheme() {
  static const Theme value = correct(derive(makeDark()));
  return value;
}

const Theme& lightTheme() {
  static const Theme value = correct(derive(makeLight()));
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
  // everywhere else these files are read. The hues are pulled toward the
  // palette's own blue-slate family rather than Obsidian's: note takes the
  // accent itself, and the other four are the hues that stay distinguishable
  // from it -- teal, violet, amber, red.
  unsigned hue = light ? 0x2F6FD6 : 0x729ED8;
  if(kind == "TIP") hue = light ? 0x1D7F95 : 0x8ED4E6;
  else if(kind == "IMPORTANT") hue = light ? 0x7B4CC3 : 0xCBA6FF;
  else if(kind == "WARNING") hue = light ? 0xA86911 : 0xE4B77B;
  else if(kind == "CAUTION") hue = light ? 0xC3414B : 0xCD7A82;

  CalloutStyle style;
  style.accent = rgb(hue);
  // The tint is the accent laid over the page rather than a second constant,
  // so a palette change carries it along.
  const SDL_Color page = light ? lightTheme().editorBackground : darkTheme().editorBackground;
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


std::string calloutLabel(std::string_view kind) {
  // Normalised from the kind, not echoed from the source. The two scanners
  // hand this function different bytes for the same callout: `doc::BlockScan`
  // keeps `[!WARNING]` verbatim, md4c lower-cases it on the way through. The
  // old version only lower-cased the *tail*, so the live surface headed that
  // callout "Warning" and the reading pane headed it "warning" -- the same
  // note, the same block, two labels, which is exactly the drift `calloutStyle`
  // beside this already normalises its input to avoid.
  //
  // It also made the label depend on how the author typed the tag: `[!note]`
  // read as "note" and `[!NOTE]` as "Note" in the same pane, in the same note.
  if(kind.empty()) return "Note";
  std::string name(kind);
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  for(std::size_t i = 1; i < name.size(); ++i) {
    name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
  }
  return name;
}

}
