#pragma once

#include "ui/Rect.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

// The marks every surface is built out of: a filled rect, a stroked one, a
// rule, and the three compounds the whole shell shares -- a surface, a list row
// and a focus ring.
//
// Its own header because it is the bottom of the drawing stack: everything
// above it is built on these, while these depend on nothing but the palette and
// a rect. They were the first forty lines of `ui/Draw.h`, which by the end
// declared them alongside the text renderer, the image cache, eleven glyphs, a
// scrollbar and ten widgets -- five layers of one stack under one name, so a
// surface that wanted to fill a rectangle included the picture decoder.
namespace micronotes::ui {

// One-time renderer setup the palette depends on.
//
// Seven colours in `Theme` carry an alpha on purpose: the text selection, the
// find-match fill and its border, the scrollbar track and thumb border, and the
// raised-surface sheen. SDL's default draw blend mode is SDL_BLENDMODE_NONE,
// which writes the alpha byte and then ignores it, so every one of those painted
// opaque -- a scrollbar track set to alpha zero, meaning "do not draw me", drew
// a solid bar down the edge of every list instead.
//
// Worse, it was not consistent within a run. The overlay stack turned blending
// on before dimming the window behind a palette and never turned it back off,
// so a session that had opened the command palette once painted the rest of the
// shell differently from one that had not. Blending belongs to the renderer for
// its whole life, and this is where that is said.
void configureRenderer(SDL_Renderer* renderer);

void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color);
void hLine(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color);

// Nothing here rounds a corner.
//
// The shell used to have three radii -- one for controls, one for blocks in the
// page, one for what floats over it -- which is three answers to a question a
// flat interface does not ask. What separates two surfaces now is that their
// fills differ and, where that is not enough, a single-weight 1px rule; and
// what makes a control a control is its ground, not its silhouette. See
// `Theme`'s contrast corrector, which is what makes that hold up.
void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor, SDL_Color borderColor);
void drawSurface(SDL_Renderer* renderer, Rect rect);

// A row in a list: selected, pointed at, or neither.
//
// Hover and selection share a ground, and the 2px strip of accent down the
// leading edge is the whole of what says "this one" rather than "this is what
// I would click". Three shades for three states is three shades a reader cannot
// tell apart; a strip is a difference in kind.
void drawRow(SDL_Renderer* renderer, Rect row, SDL_Color base, bool emphasized,
             bool accentStrip = false);
// The same, against the panel ground, for the callers that have no other base.
void drawRow(SDL_Renderer* renderer, Rect row, bool selected, bool hot);

// Which surface has the keyboard, as a 1px outline round the pane. An outline
// rather than an edge strip: the strip is what a selected *row* wears, and one
// device cannot mean two things.
void drawFocusRing(SDL_Renderer* renderer, Rect pane, bool focused);

}
