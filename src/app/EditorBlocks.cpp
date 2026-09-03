#include "app/EditorBlocks.h"

namespace micronotes::app {

doc::BlockSpan editorBlocks(const UiRuntime& ui) {
  // `+ 1` because `drawLive` stamps the layout with the editor's revision plus
  // one: zero means "cannot say" to the layout's reuse check, so the stamps are
  // shifted by one to keep a fresh editor's revision 0 from meaning that.
  return ui.livePage.blocksAt(ui.editor.revision() + 1ull);
}

EditorBlocks::EditorBlocks(const UiRuntime& ui) : lent_(editorBlocks(ui)) {
  // An empty borrow means there was no borrow: the layout holds at least one
  // block for any buffer, an empty note included.
  if(!lent_.empty()) return;
  owned_ = doc::scanBlocks(ui.editor.text());
}

}
