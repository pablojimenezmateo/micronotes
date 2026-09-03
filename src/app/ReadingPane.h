#pragma once

#include "CoreAliases.h"

#include "app/Shell.h"
#include "core/markdown/MarkdownParser.h"
#include "ui/Draw.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <string>

// The reading pane: the open note rendered through md4c, with no caret.
//
// It came out of Application.cpp, where it was 230 lines of geometry written
// inline -- and written *twice*, because a measure pass walked the whole
// document to find how far it scrolls and a draw pass then walked it again to
// paint it. Two parallel walks of the same blocks disagreed in three places
// (an Html block's bottom spacing, an image's rounding, a callout's label case),
// each of which is a scroll extent or a box that does not match the text in it.
//
// There is one walk now, it is memoised against the note and the geometry, and
// the draw reads block positions out of it -- so a scroll or a hover costs the
// viewport rather than the note, and the two passes cannot drift because there
// is only one.
namespace micronotes::app {

// A heading's slug, as an in-note `#link` spells it. Shared with the link
// handler, which has to turn the other half of the same link into the same
// string.
std::string anchorFor(std::string value);

// The note the reading pane is showing, parsed. Cached against the buffer's own
// bytes, because a parse is the whole note and a frame happens for reasons that
// have nothing to do with the text.
const markdown::Document& previewDocument(UiRuntime& ui);

void drawReadingPane(SDL_Renderer* renderer, ui::TextRenderer& text, ui::ImageCache& images,
                     UiRuntime& ui, ui::Rect rect);

}
