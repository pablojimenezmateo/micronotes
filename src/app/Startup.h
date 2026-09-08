#pragma once

#include "app/Application.h"

// Applying the command line to a shell that has no window yet.
//
// Two halves, split by what they need. `applyStartupOptions` runs before SDL is
// initialised: it settles which library is open, which notes are in tabs, which
// panes and panels are showing. Everything it does is state, which is what makes
// `--headless` possible at all -- the harness and the tests drive exactly this
// and then stop.
//
// `applyWindowOptions` is the rest, and needs a window: the query typed into
// the search box (which takes focus) and `--open`'s overlay.
//
// Here rather than in `run()` because a fifth of that function was a list of
// `if(options.x)`, and nothing in the list has anything to do with the event
// loop underneath it. `--select` in particular is a small parser, and it was
// sitting inside the function that owns the renderer.
namespace micronotes::app {

struct UiRuntime;

// What the options say about the shell's state. False when a fatal option
// failed -- an unwritable config path, a library that will not open, an
// attachment that could not be filed -- and it has already said why on stderr.
bool applyStartupOptions(UiRuntime& ui, ApplicationOptions& options);

// What the options say about the first frame. Reports an unknown `--open`
// value on stderr and otherwise cannot fail.
void applyWindowOptions(UiRuntime& ui, const ApplicationOptions& options);

}
