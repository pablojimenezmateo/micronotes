#pragma once

namespace micronotes::app {

struct UiRuntime;

// The menus a right click opens, anchored where the pointer was.
void openNoteMenu(UiRuntime& ui, float x, float y);
void openFolderMenu(UiRuntime& ui, float x, float y);

}
