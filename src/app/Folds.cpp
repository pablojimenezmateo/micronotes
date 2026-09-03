#include "app/Folds.h"

#include "CoreAliases.h"
#include "core/editor/MarkdownEditor.h"
#include "doc/Fold.h"
#include "ui/FoldState.h"

#include <functional>
#include <utility>

namespace micronotes::app {

NoteFolds noteFolds(ui::FoldState& folds, const editor::MarkdownEditor& editor,
                    std::string noteId) {
  NoteFolds out;
  if(folds.anyFolded(noteId)) {
    out.page.collapsed = [&folds, &editor, noteId](const doc::SourceBlock& block) {
      return folds.folded(noteId, doc::foldKey(editor.text(), block));
    };
  }
  out.page.expand = [&folds, &editor, noteId](const doc::SourceBlock& block) {
    folds.unfold(noteId, doc::foldKey(editor.text(), block));
  };
  // What the layout's reuse check needs so it does not re-derive the fold state
  // from the document. Never zero, because zero means "cannot say"; and the
  // note is mixed in, since switching notes changes what `collapsed` answers
  // without the fold state moving at all.
  out.stamp = folds.revision() * 1000003ull + std::hash<std::string> {}(noteId) + 1ull;
  return out;
}

}
