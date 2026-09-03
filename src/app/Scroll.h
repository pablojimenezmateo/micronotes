#pragma once

#include "app/Shell.h"
#include "ui/Draw.h"

// Where a wheel event goes.
//
// Deciding which of the five scrolling surfaces a gesture belongs to is
// deterministic geometry, so it is here rather than in the run loop's event
// switch: the rule is one function, readable in one screen, and the run loop
// hands it a delta instead of carrying the precedence order inline.
//
// The precedence is the same one every desktop shell uses -- the surface under
// the pointer wins, and only when nothing is under the pointer does the pane
// with focus take it -- with an open overlay ahead of all of them, because a
// palette that scrolled the note behind it would be a palette you could not
// read to the end of.
namespace micronotes::app {

// Applies one wheel event. `notches` is SDL's `wheel.y`, positive when the
// content should move down.
void routeWheel(ui::TextRenderer& text, UiRuntime& ui, float notches, int width, int height);

}
