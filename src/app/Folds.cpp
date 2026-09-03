#include "app/Folds.h"

#include "CoreAliases.h"
#include "core/editor/MarkdownEditor.h"
#include "doc/Fold.h"
#include "ui/FoldState.h"

#include <functional>
#include <string>

namespace micronotes::app {

PageFolds livePageFolds(UiRuntime& ui) {
  PageFolds page;
  page.collapsed = [&ui](const doc::SourceBlock& block) {
    return ui.folds.folded(ui.state.selection().noteId, doc::foldKey(ui.editor.text(), block));
  };
  page.expand = [&ui](const doc::SourceBlock& block) {
    ui.folds.unfold(ui.state.selection().noteId, doc::foldKey(ui.editor.text(), block));
  };
  return page;
}

NoteFoldStamp noteFoldStamp(const ui::FoldState& folds, std::string_view noteId) {
  NoteFoldStamp out;
  out.anyFolded = folds.anyFolded(noteId);
  // Never zero, because zero means "cannot say"; and the note is mixed in,
  // since switching notes changes what `collapsed` answers without the fold
  // state moving at all.
  out.stamp = folds.revision() * 1000003ull + std::hash<std::string_view> {}(noteId) + 1ull;
  return out;
}

}
