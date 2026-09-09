#pragma once

#include "CoreAliases.h"

#include "core/editor/TextField.h"
#include "library/SearchScope.h"
#include "ui/Rect.h"

// The five one-line text fields the shell keeps.
//
// Together because which of them is live is a function of `ui.focus` and
// nothing else, and every key, paste and middle click that reaches a field has
// to ask that question first -- `app/Fields.h` is the one place that answers
// it. As five loose fields beside forty others, the fact that they are one set
// with one router was not visible from either end.
//
// The search scope belongs to the search box rather than to the shell: it is
// half of the question the box is asking, and the two are always set together.
namespace micronotes::app {

struct TextFields {
  editor::TextField search;
  library::SearchScope searchScope = library::SearchScope::All;
  editor::TextField find;
  editor::TextField tag;
  editor::TextField rename;
  editor::TextField folderRename;

  // Where the live one was drawn this frame, recorded by the draw for the same
  // reason the menu bar's targets are: the answer needs a measured string and
  // the pointer's motion arrives with no renderer in scope.
  //
  // It is what makes a *drag* across one of these fields select text. The
  // anchor was already being recorded on press -- `ui.fieldSelect` -- and
  // nothing ever extended it, because nothing outside the drawing code knew
  // where the field's text had ended up. So a press set an anchor, the drag did
  // nothing, and the mouse-up published a selection that was never made.
  // Empty when no field has the keyboard, which is what the motion arm checks.
  ui::Rect drawnRect;
};

}
