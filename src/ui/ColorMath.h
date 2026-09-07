#pragma once

#include <SDL3/SDL_pixels.h>

// Colour-space maths. Pure functions over colours, with no theme and no state,
// so a palette can be checked for legibility in a test rather than by squinting
// at a screenshot.
namespace micronotes::ui {

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
SDL_Color ensureBackgroundSeparation(SDL_Color background, SDL_Color reference,
                                     float minimumContrast);

// The ratios the interface is held to. Body text and anything a reader has to
// make out word by word take the full AA ratio; labels, counts and markers are
// large or incidental enough for the large-text ratio.
inline constexpr float kTextContrast = 4.5f;
inline constexpr float kIncidentalContrast = 3.0f;

// What two grounds have to differ by to read as two grounds. Far below the text
// ratios -- a panel one step off the page is a panel, not illegible text -- and
// the number microide arrived at for the same job.
inline constexpr float kSurfaceSeparation = 1.08f;

}
