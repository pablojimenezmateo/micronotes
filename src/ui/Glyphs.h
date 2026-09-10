#pragma once

#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <span>
#include <string_view>

// Every mark the shell draws instead of typesetting.
//
// One reason covers all of them, and it is the same reason each time: the
// chrome face is a mono programming face, which carries none of these, and the
// face that does carry them -- a colour emoji font -- is a single fixed bitmap
// strike that has to be resampled to whatever size the shell actually uses. A
// note's icon is drawn at sixteen pixels beside its name, which is exactly
// where a resampled 136px bitmap looks worst. Two lines stay exact at any size.
namespace micronotes::ui {

// A folder's disclosure mark: two short strokes meeting at a point, down when
// open and right when shut. Drawn rather than typeset -- the mono face has no
// glyph for one, and two lines stay exact at any scale where a glyph would be
// resampled into a smudge.
void drawChevron(SDL_Renderer* renderer, float x, float centerY, bool open, SDL_Color color);

// A close cross, and a tick. Both drawn for the same reason as the chevron.
void drawCloseGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);
void drawCheckGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);
// The revert mark on a setting that has been moved off its default: three sides
// of a square with an arrowhead where the fourth would close it. Drawn open, so
// it does not read as the checkbox two rows up.
void drawResetGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);
// Which way a single-headed arrow points. An enum rather than the `pointRight`
// bool this was: the find bar's step buttons want up and down, and a second
// spelling for the other axis is how a shell ends up with two arrow glyphs that
// do not match.
enum class ArrowDirection {
  Up,
  Down,
  Left,
  Right
};

// A single-headed arrow, for the tab strip's overflow buttons, the find bar's
// step buttons and anything else that moves along a run.
void drawArrowGlyph(SDL_Renderer* renderer, Rect box, ArrowDirection direction, SDL_Color color);

// The mark at the end of a line the column broke: a shaft turning back on
// itself, which is what a continuation is shown as everywhere it is shown.
// Drawn rather than typeset for the reason every mark here is -- the mono face
// carries no U+21A9, and the face that does is a bitmap strike that would be
// resampled to eight pixels.
void drawWrapGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);

// A magnifier, for the search field.
void drawSearchGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);

// A five-pointed star, one of the marks a note can wear. Filled or as an
// outline of the same silhouette. Drawn rather than typeset because the chrome
// face is a mono programming face and has neither star -- the breadcrumb used
// to set them from the proportional face, and switching the chrome to mono
// left tofu where the star had been.
void drawStarGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color);

// A thumbtack: filled when the thing is pinned, an outline when pinning it is
// on offer. One mark for both kinds of pinning the shell has -- a note pinned
// to the sidebar's PINNED band, and a tab pinned so nothing evicts it -- so
// that the two read as the same gesture, which is what they are.
void drawPinGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color);

// One of the marks a note can wear beside its name, and what the picker calls
// it. The id is what goes into the note's front matter, so it has to stay
// stable: a library written by one version is read by the next.
struct NoteGlyph {
  std::string_view id;
  std::string_view label;
};

// The whole set, in picker order.
std::span<const NoteGlyph> noteGlyphs();

// Draws one of them centred in `box`. False when `id` names none of ours --
// an empty icon, or one written by a version that offered a different set --
// so the caller can fall back to the mark a note with no icon wears.
bool drawNoteGlyph(SDL_Renderer* renderer, std::string_view id, Rect box, SDL_Color color);

// A companion file's mark: a page with its corner folded. One mark for every
// kind of file, because the tree says what a file is called and the desktop
// says what opens it; a mark per type would be a MIME table drawn in 12 pixels.
void drawFileGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color);

// Minimise, maximise (or restore), close: `which` is 0, 1, 2 in that order,
// which is the order they are laid out in.
void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized,
                     SDL_Color color);

// A tag's colour, as the dot drawn beside its name and at the trailing edge of
// every note carrying it. Always a solid disc: see the note on the definition
// for why there is no second state.
void drawTagDot(SDL_Renderer* renderer, Rect box, SDL_Color color);

}
