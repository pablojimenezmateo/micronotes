#pragma once

#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

// What a press on the note itself means.
//
// The page is the shell's most crowded target by a long way. On the live
// surface, one press has to choose between a scrollbar thumb, a code block's
// copy button, a link, the formatting toolbar, a fold chevron, the gutter's
// insert button, the gutter's drag handle, a task checkbox, a `Complex` block
// to drop into raw source, and -- when it is none of those -- the caret. The
// order those are tested in *is* the design: chrome floats over text, so chrome
// wins; and the two that are chrome-but-not-clicks (the fold and the copy
// button) deliberately leave the caret and the selection where they were.
//
// This was a hundred and thirty lines inside `handleMouse`, which is why that
// function was three hundred: everything else in the press chain is one line
// handing off to the surface that owns the band. Now this is too.
namespace micronotes::app {

struct UiRuntime;

// A press on one of the three panes' scrollbar thumbs. True when a thumb was
// grabbed, in which case the drag is now live and nothing else may claim the
// press.
bool pressPaneScrollbar(UiRuntime& ui, ui::Rect content, float x, float y, Uint8 button);

// A press inside the content column. Always claims it: a click on the page is
// the page's, even where it lands on nothing in particular -- that is what puts
// the caret there.
void pressPage(ui::TextRenderer& text, UiRuntime& ui, ui::Rect content, float x, float y,
               Uint8 button);

}
