#pragma once

#include <string>
#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// The menus a right click opens, anchored where the pointer was.
void openNoteMenu(UiRuntime& ui, float x, float y);
void openFolderMenu(UiRuntime& ui, float x, float y);

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

}
