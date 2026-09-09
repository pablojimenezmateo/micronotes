#pragma once

#include "ui/Rect.h"

#include <string>

// Where a link the panes drew this frame landed.
//
// Three fields, and its own header because of who needs them. It lived in
// `app/Shell.h`, so `app/InlineText.h` and `app/MarkdownBlocks.h` -- which
// draw a run of formatted text and know nothing about a running shell -- each
// included the whole runtime to name the type in one parameter. That is the
// coupling that stops a drawing helper from being testable on its own.
namespace micronotes::app {

struct LinkRegion {
  ui::Rect rect;
  std::string target;
  // See PageLink::wiki: a link to a note is followed differently from a link
  // to a file, and the two are indistinguishable once they are just strings.
  bool wiki = false;
};

}
