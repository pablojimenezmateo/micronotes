#pragma once

#include "CoreAliases.h"

#include "core/editor/TextField.h"
#include "library/SearchScope.h"

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
};

}
