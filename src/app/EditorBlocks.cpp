#include "app/EditorBlocks.h"

namespace micronotes::app {

doc::BlockSpan editorBlocks(const UiRuntime& ui) {
  return ui.readingPage.blocksAt(layoutRevision(ui.editor.revision()));
}

EditorBlocks::EditorBlocks(const UiRuntime& ui) : lent_(editorBlocks(ui)) {
  // An empty borrow means there was no borrow: the layout holds at least one
  // block for any buffer, an empty note included.
  if(!lent_.empty()) return;
  owned_ = doc::scanBlocks(ui.editor.text());
}

}
