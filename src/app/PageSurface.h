#pragma once

#include "CoreAliases.h"

#include "app/PageView.h"
#include "app/Shell.h"
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

// What the two page surfaces share.
//
// The live page and the reading page are one renderer with different things
// turned on, and `drawLive` and `drawReading` were the same fifteen calls in
// the same order with nine extra in the middle of one of them. Three of those
// calls are the same three hooks installed with the same three lambda bodies;
// six are one feeding sequence; and three more are what both do once the layout
// has settled and once the paint has run.
//
// The nine that differ are what a *live* page is -- folds, a block selection, a
// drop indicator, a caret, a raw block. That is why this is three shared
// functions and a `PageFrame` rather than one page: the shared part is the
// contract, not the surface.
namespace micronotes::app {

// The hooks every page installs, and the fold predicate is not one of them:
// this is measure and draw a block the scanner does not model, resolve a
// wikilink, and fit a picture.
//
// Installed once, for the life of the process. Each closure captures more than
// a `std::function`'s inline buffer holds, so rebuilding them per frame was
// five heap allocations and five frees on a frame that draws several hundred
// runs. They read the current note and the current buffer *when they are
// called* rather than capturing either, which is what makes installing them
// once correct rather than merely cheaper -- and the renderer, the text
// renderer and the runtime are created in `run()` and outlive every frame,
// which is the lifetime these references need.
void wirePage(PageView& page, SDL_Renderer* renderer, ui::TextRenderer& text,
              ui::ImageCache& images, UiRuntime& ui);

// The inputs both surfaces take from the shell, with the live page's own left
// at their defaults for it to fill in. See `PageFrame` for why this is one
// value rather than a sequence of calls.
PageFrame pageFrameFor(ui::TextRenderer& text, ui::ImageCache& images, UiRuntime& ui);

// Between the layout and the paint, in this order.
//
// A queued cross-note anchor can only be resolved now, because the page is only
// now holding the note the link opened -- see `queueAnchorJump`. And the parse
// cache mirrors this page's `Complex` blocks, so this is the only place that
// knows which of them the note no longer contains; one comparison, on a frame
// that laid out nothing new.
void settlePageLayout(UiRuntime& ui, PageView& page);

// After the paint: the note's header, and the links the shell hit-tests.
//
// The header is clipped to the page so it scrolls off the top rather than
// running up over the tab strip on its way out. The links are filled by the
// paint, which is why this cannot be folded into `settlePageLayout`.
void publishPageChrome(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                       PageView& page);

}
