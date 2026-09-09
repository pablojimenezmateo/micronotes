#pragma once

#include <string>
#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// The menus a right click opens, anchored where the pointer was.
void openNoteMenu(UiRuntime& ui, float x, float y);
void openFolderMenu(UiRuntime& ui, float x, float y);

// A companion file's menu, and a files directory's. Both are about
// `ui.sidebar.companionTarget`, which the press that opened them set: a
// companion is never the selection, so the row has to be remembered some other
// way. See `library::kFilesDirName`.
void openFileMenu(UiRuntime& ui, float x, float y);
void openFilesFolderMenu(UiRuntime& ui, float x, float y);

// A tab's menu. `noteId` is the tab under the pointer, not the note on the
// page: a right click on a tab is about that tab, and switching to it first
// would be the menu acting before it was asked to.
//
// Right-clicking a tab did nothing at all before. The strip answered a left
// click and a middle click and ignored the third button, which on a tab strip
// is where every application puts exactly this.
void openTabMenu(UiRuntime& ui, std::string_view noteId, float x, float y);

// A tag's own menu: filter by it, and choose what colour it is.
//
// Right click rather than a control on the row, because a tag row's whole width
// already means "filter by this" and a second target on it would be a target
// competing with the row it sits in. Reachable from the TAGS band and from any
// note's dots, which are the two places a tag is drawn.
void openTagMenu(UiRuntime& ui, std::string_view tag, float x, float y);

// The swatch grid. A tag's colour is an index into a fixed palette rather than
// an arbitrary RGB -- see `ui::TagColors` for why -- so the picker is that
// palette laid out to be pointed at, plus a way back to the derived default.
void openTagColorPicker(UiRuntime& ui, std::string tag);

// The same grid, for the mark a note wears beside its name. Its first cell is
// "no icon", so a note can be stripped of one without a second gesture.
void openIconPicker(UiRuntime& ui);

}
