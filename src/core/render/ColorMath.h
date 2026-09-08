#pragma once

#include <SDL3/SDL_pixels.h>

// Colour-space maths. Pure functions over colours, with no theme and no state,
// so a palette can be checked for legibility in a test rather than by squinting
// at a screenshot.
namespace microcore::render {

// WCAG relative luminance of an sRGB colour. Alpha is ignored: a translucent
// colour's luminance depends on what is behind it, which is what compositeOver
// is for.
float relativeLuminance(SDL_Color color);

// WCAG contrast ratio between two colours. Always >= 1, and symmetric.
float contrast(SDL_Color a, SDL_Color b);

// Linear interpolation between two colours. `amount` is clamped to [0, 1] and
// the result keeps `base`'s alpha.
SDL_Color blend(SDL_Color base, SDL_Color tint, float amount);

// Alpha-over composite of `foreground` onto `background`. The result is opaque,
// which is what makes it the right way to ask what a translucent fill will
// actually look like once drawn.
SDL_Color compositeOver(SDL_Color foreground, SDL_Color background);

// Whether a colour reads as a light surface. The threshold is the luminance at
// which black and white text have equal contrast against it.
bool isLight(SDL_Color color);

// `foreground` moved toward black or white -- whichever the background is not
// -- until it reaches `minimumContrast` against it. Returns the original when
// it already passes, and the best it managed when the ratio is unreachable,
// because a foreground that is merely too low is still better than one thrown
// away.
//
// This is what stops a hand-picked palette, or one from a theme file, from
// producing text nobody can read.
SDL_Color ensureContrast(SDL_Color foreground, SDL_Color background, float minimumContrast);

// `base` moved toward white or black by `amount`. Named because "blend with
// white" is what a reader has to decode at every call site otherwise, and the
// two are used in pairs -- a role is lightened on a dark palette and darkened
// on a light one, which is one expression with `isLight` in front of it.
SDL_Color lighten(SDL_Color base, float amount);
SDL_Color darken(SDL_Color base, float amount);

// Whether two colours sit on the same side of the light/dark threshold. A
// palette that mixes them has a panel that reads as a hole in one theme and as
// a raised card in the other.
bool samePolarity(SDL_Color a, SDL_Color b);

// `background` pushed away from `reference` until the two are at least
// `minimumContrast` apart, in whichever direction the reference is not.
//
// The counterpart of `ensureContrast` for the grounds rather than the ink. A
// flat interface has no shadows and no radii, so the only thing saying "this
// panel is not that panel" is that their fills differ -- and two fills a
// fraction of a step apart say nothing at all. One nudge rather than a search:
// the surfaces are derived from each other, so correcting one by a known amount
// keeps the family recognisable, where hunting for a ratio would not.
//
// **Not for a palette somebody picked.** A single 0.12 nudge is a whole step on
// a dark ground, so applying it to a hand-chosen colour that misses the floor
// by a hair replaces the decision rather than correcting it -- see `Theme`'s
// `correct`, which used to do exactly that to the chrome and turned the menu
// bar into a mid grey. This is for grounds *derived* from a palette that
// arrived from outside, where there was no decision to override.
SDL_Color ensureBackgroundSeparation(SDL_Color background, SDL_Color reference,
                                     float minimumContrast);

// The ratios the interface is held to. Body text and anything a reader has to
// make out word by word take the full AA ratio; labels, counts and markers are
// large or incidental enough for the large-text ratio.
inline constexpr float kTextContrast = 4.5f;
inline constexpr float kIncidentalContrast = 3.0f;

// What two grounds have to differ by to read as two grounds. Far below the text
// ratios: a panel one step off the page is a panel, not illegible text.
//
// A floor that *describes* the palette rather than one the palette is bent to
// meet. microide, whose values these are, has no single number for this -- it
// asks 1.04 of a panel against the page, 1.08 of a raised control, and 1.12 of
// the active tab against its strip, each at the one site that derives that
// ground. 1.05 is the floor common to all of them, and the one the built-in
// palette's tightest deliberate pairing clears: the chrome against the page it
// frames, at 1.057 on the dark theme.
//
// It was 1.08, which that pairing misses, and the corrector duly "fixed" it.
// The lesson is in which direction the disagreement got resolved: a constant
// that a carefully chosen palette fails is a wrong constant, not a wrong
// palette.
inline constexpr float kSurfaceSeparation = 1.05f;

}
