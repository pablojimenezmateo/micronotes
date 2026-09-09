#pragma once

#include "ui/TextRenderer.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

namespace micronotes::app {

struct UiRuntime;

// What sits above a note's first block: the front matter it carries.
//
// The front matter was parsed, preserved on save, and then hidden, so a note
// with an `aliases:` key looked exactly like one without.
//
// It is not body text. The properties are the file's header, so editing them
// goes through the library rather than through the buffer -- nothing here ever
// writes a byte into the note's Markdown.
//
// The note's *name* used to be drawn here too, large, above the properties, and
// clicking it renamed the note in place. It is gone: the breadcrumb and the tab
// both name the note already, and a third copy of it took a title's worth of
// room off the top of every page. Renaming is the `rename-note` prompt, which
// is where every other route to it already went.

// How tall the header is for the note that is open, or zero when nothing is.
// Called before the page lays out, because the page reserves this room at the
// top of its scrolling space -- the header scrolls away with the content rather
// than sitting over it, which is what makes the note feel like one document.
//
// Takes a mutable runtime because measuring is what refreshes the cache. The
// alternative -- measure from whatever the last draw left behind -- gives the
// frame a note is opened on the previous note's header height, and the page
// beneath it starts a header too high or too low for exactly one frame.
//
// The renderer is unused now that the title is gone -- the property rows are a
// fixed height apiece -- and stays because the height is measured at every call
// site that has one, and a row here that does depend on the font is the obvious
// next thing to add.
float pageHeaderHeight(ui::TextRenderer& text, UiRuntime& ui);

// Draws it into `column` with its top at `top`, both in window coordinates.
void drawPageHeader(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect column, float top);

}
