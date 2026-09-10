#pragma once

#include "app/PageView.h"
#include "ui/ImageCache.h"

#include <SDL3/SDL.h>

// How a note's `![alt](target)` becomes a picture on a `PageView`.
//
// The page holds a buffer, not a library and not a texture cache, so it asks
// for an image's box and hands the drawing back -- exactly as it asks whether a
// `[[wikilink]]` resolves. This is the answer, and there is one of it: both
// panes are the same renderer, so a picture that is fitted one way in the raw
// pane and another way in the reading pane is the class of divergence the
// merge exists to remove.
namespace micronotes::app {

struct UiRuntime;

// Fills in `measureImage` and `drawImage`. The renderer, the cache and the
// runtime all outlive every frame, which is the lifetime these references need.
void wirePageImages(PageViewHooks& hooks, SDL_Renderer* renderer, ui::ImageCache& images,
                    UiRuntime& ui);

// Moves whenever the cache could answer differently -- a texture that has
// finished loading changes the height of the block showing it. Handed to
// `PageView::setImageRevision` once a frame.
std::uint64_t pageImageRevision(const ui::ImageCache& images);

}
