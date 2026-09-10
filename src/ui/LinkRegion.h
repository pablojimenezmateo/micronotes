#pragma once

#include "ui/Rect.h"

#include <string>

// Where a link a surface drew this frame landed.
//
// Three fields, and its own header because of who needs them. It was two
// types: `app::LinkRegion`, which the shell collects per frame, and
// `app::PageLink`, which `PageView` handed back -- the same rect, the same
// target and the same `wiki` flag, written out twice with two paragraphs of
// comment each, plus a loop in `app/ReadingPage.cpp` copying one into the
// other field by field.
//
// It lives here rather than in `app/` because a run painter is what fills it
// in, and that painter is `ui/DocRuns.h`: a file that draws a run of formatted
// text and knows nothing about a running shell cannot include `app/Shell.h` to
// name the type in one parameter. That coupling is what kept the screen's two
// run loops from being one, which was TD-43.
namespace micronotes::ui {

struct LinkRegion {
  Rect rect;
  std::string target;
  // A `[[wikilink]]` names a note; every other link names a file or a URL.
  // They look the same as strings and are followed in completely different
  // ways, so which one it is travels with the rect rather than being guessed
  // at from the target's shape.
  bool wiki = false;
};

}
