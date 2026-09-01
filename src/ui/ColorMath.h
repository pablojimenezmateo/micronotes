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

// The ratios the interface is held to. Body text and anything a reader has to
// make out word by word take the full AA ratio; labels, counts and markers are
// large or incidental enough for the large-text ratio.
inline constexpr float kTextContrast = 4.5f;
inline constexpr float kIncidentalContrast = 3.0f;

}
