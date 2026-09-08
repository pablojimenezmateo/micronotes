#pragma once

#include "CoreAliases.h"

#include "app/Wheel.h"
#include "core/editor/SoftWrap.h"
#include "ui/Memo.h"
#include "ui/Settings.h"

#include <cstdint>
#include <vector>

// The pane that shows the note as a monospaced file.
//
// Named apart from the other two panes because it is a third renderer, and the
// plan on the record is that it may go: `docs/tech-debt.md` TD-14 says the file
// is "kept apart so that replacing it is a matter of deleting one file", and
// this is the state half of that promise. If the pane goes, this header goes
// with it and nothing else has to be untangled -- which was not true while its
// five fields sat among `UiRuntime`'s hundred.
namespace micronotes::app {

// What the pane's soft wrap was computed from: the buffer, the column, and the
// face it was measured in.
//
// The pane shows the file as bytes, so its line breaks are the file's rather
// than the layout's -- which is why it is the one surface in the shell with a
// wrap of its own to memoise. Keyed on the editor's revision rather than on a
// copy of the note; see `editorRows` for what that was costing.
struct RawRowsKey {
  std::uint64_t revision = 0;
  int wrapWidth = -1;
  ui::TextSize textSize = ui::TextSize::Medium;
  bool operator==(const RawRowsKey&) const = default;
};

struct RawPaneState {
  ui::Memo<std::vector<editor::SoftWrapRow>, RawRowsKey> rows;
  ScrollList list;
  // Rows the pane last had room for, so PageUp/PageDown match the view.
  int visibleRows = kEditorPageLines;
};

}
