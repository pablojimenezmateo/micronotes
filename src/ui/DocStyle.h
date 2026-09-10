#pragma once

#include "doc/BlockScan.h"
#include "doc/Layout.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"

#include <SDL3/SDL_pixels.h>

// How a `doc::` value is drawn: a run's style becomes a face, a run's role
// becomes ink.
//
// Both mappings are decisions, and both had two copies -- one in
// `app/PageViewStyle.cpp` for the screen and one in `export/PdfBlocks.cpp` for
// the page, the second carrying a comment saying it was the first one's table
// read against a different palette. Two copies of a decision is one that
// drifts, and this one had already drifted: the screen sent `TextRole::Code`
// to `textPrimary` and the exporter fell through to the same value by
// accident, so adding a role to the enum would have left the exporter silently
// inking it as body text.
//
// It lives in `ui/` because that is the lowest layer that has both a
// `doc::TextRole` and a `ui::Theme` in scope -- `doc/` deliberately names no
// colour at all, and `export/` and `app/` are both above here.
namespace micronotes::ui {

// The face a run is set in. Size travels through unchanged: a `RunStyle` size
// is already in logical pixels, which is what `TextStyle` wants.
TextStyle textStyleFor(const doc::RunStyle& style);

// The ink a run takes against this palette.
SDL_Color inkFor(const Theme& theme, doc::TextRole role);

// The same, for a run inside a block whose kind changes what body text means:
// a quote reads as someone else's words, so its body sits a step back from the
// page's own text. Markers and links keep their own roles.
SDL_Color inkFor(const Theme& theme, doc::TextRole role, doc::BlockKind kind);

}
