#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// What sits above a note's first block: the note's own name, and the front
// matter it carries.
//
// Both are things the library already knows and the page used to say nothing
// about. The title was only ever in the breadcrumb, thirty pixels high at the
// top of the window, which is not where anyone looks to find out what they are
// reading; the front matter was parsed, preserved on save, and then hidden, so
// a note with an `aliases:` key looked exactly like one without.
//
// Neither is body text. The title is the file's name and the properties are its
// header, so editing either goes through the library rather than through the
// buffer -- nothing here ever writes a byte into the note's Markdown.

// How tall the header is for the note that is open, or zero when nothing is.
// Called before the page lays out, because the page reserves this room at the
// top of its scrolling space -- the header scrolls away with the content rather
// than sitting over it, which is what makes the note feel like one document.
//
// Takes a mutable runtime because measuring is what refreshes the cache. The
// alternative -- measure from whatever the last draw left behind -- gives the
// frame a note is opened on the previous note's header height, and the page
// beneath it starts one title too high or too low for exactly one frame.
float pageHeaderHeight(ui::TextRenderer& text, UiRuntime& ui);

// Draws it into `column` with its top at `top`, both in window coordinates.
// Records the title's rect on the runtime so a click can find it.
void drawPageHeader(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect column, float top);

// Starts editing the title when the click landed on it. Returns whether it did.
bool handlePageHeaderClick(UiRuntime& ui, float x, float y);

// What a click somewhere else means for a title that is being edited.
enum class TitleEdit {
  None,      // nothing was being edited, or the click was on the title itself
  Kept,      // the name changed and the caller should rename the note
  Abandoned  // nothing changed; the edit has been closed already
};

// Clicking away from a title being edited keeps the new name. `Esc` is how you
// throw one away; a click somewhere else is not, and losing a typed name to one
// would be the shell quietly discarding work.
//
// The rename itself is the caller's, because renaming a note is a library
// operation with a status line and a reload behind it, and the header has no
// business reaching into any of that. This says only whether one is wanted.
TitleEdit pageHeaderClickAway(UiRuntime& ui, float x, float y);

}
