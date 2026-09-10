#pragma once

#include "doc/Layout.h"
#include "ui/LinkRegion.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <vector>

// Painting the runs of a laid-out block onto a window.
//
// The screen's counterpart of `exporting::paintRuns`, and one loop rather than
// two. It was two: `app/PageViewPaint.cpp` walked a `doc::BlockLayout`'s lines
// and runs for the note's own blocks, and `app/MarkdownBlocks.cpp` walked the
// same fields of the same type for the layouts the md4c path produces. They
// made the same decisions -- the tinted ground behind a code span, the
// strikethrough, the link rule, the region handed back to a click -- and they
// had drifted: the strikethrough sat at 0.55 of the line box in one and 0.45
// in the other, and the link rule two pixels above the bottom in one and four
// in the other, so a struck word inside a table wore its line lower than the
// same word in the paragraph above it. That was TD-43.
//
// What made one loop possible on the export side was noticing that every
// per-block ink override -- a quote's muted body, a ticked task, a callout's
// head line -- reduces to *the ink a `Body` run takes*, which is one colour
// rather than a callback. The same is true here.
//
// This is the lowest layer that can hold it: it needs a `doc::TextRole`, a
// palette and a `TextRenderer`, and `ui::LinkRegion` had to come down from
// `app/` with it.
namespace micronotes::ui {

// Where one laid-out block's text goes, what colour its body takes, and where
// the links it draws are recorded.
struct RunPaint {
  // Where the layout's own origin sits in window coordinates.
  float x = 0.0f;
  float y = 0.0f;
  // The ink a `doc::TextRole::Body` run takes. A quote's body sits back a
  // step, a ticked task is muted, a callout's head line is drawn in the kind's
  // own colour -- every one of which is a decision about the *block*, not
  // about the run, which is why one colour is enough to say it. Every other
  // role inks itself through `ui::inkFor`.
  SDL_Color bodyInk {};
  // Lines outside this band, in the layout's own space, are skipped -- which
  // is how a page draws the forty rows the window shows rather than every row
  // of the note. `to <= from` means unbanded, which is what a block small
  // enough to be drawn whole passes.
  float from = 0.0f;
  float to = 0.0f;
  // Where the rect of every link this call draws is appended, if anywhere. A
  // measure pass and a table cell drawn for its ink alone pass nothing.
  std::vector<LinkRegion>* links = nullptr;
};

// Returns how many runs were drawn, which is what the page's per-frame counter
// reports.
std::size_t paintRuns(SDL_Renderer* renderer, TextRenderer& text, const doc::BlockLayout& layout,
                      const RunPaint& paint);

}
