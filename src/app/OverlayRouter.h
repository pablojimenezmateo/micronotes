#pragma once

#include "ui/Overlay.h"

// What an answered overlay means.
//
// `ui::OverlayStack` knows how an overlay is filtered, walked and dismissed,
// and deliberately knows nothing about what any particular one is *for*: it
// hands back an `ui::OverlayResult` carrying the overlay's id, the row chosen
// and the text typed. This is the other half -- the one table mapping that id
// to the verb it stands for.
//
// It is a router and nothing else, and that is worth keeping true: every branch
// here is one line handing off to a named function, so a new overlay is an
// entry rather than an edit. It grows by one line per overlay, which is exactly
// why it does not belong in the file that also draws the frame -- as
// Application.cpp's third-largest function, it was the clearest measure of how
// much of the shell had nowhere else to live.
namespace micronotes::app {

struct UiRuntime;

void handleOverlayResult(UiRuntime& ui, const ui::OverlayResult& result);

}
