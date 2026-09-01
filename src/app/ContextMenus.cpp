#include "app/ContextMenus.h"

#include "app/Shell.h"

#include "ui/Actions.h"
#include "ui/Overlay.h"

#include <string>

namespace micronotes::app {

void openNoteMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "note-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  const bool hasNote = !ui.state.selection().noteId.empty();
  overlay.items = {
    {"new", "New note", "", ui::keysFor(ui::ActionId::NewNote), true, false},
    {"rename", "Rename", "", "", hasNote, false},
    {"icon", "Set icon", "", "", hasNote, false},
    {"tags", "Edit tags", "", ui::keysFor(ui::ActionId::EditTags), hasNote, false},
    {"favorite", "Toggle favorite", "", "", hasNote, false},
    {"move", "Move to notebook", "", "", hasNote, false},
    {"delete", "Delete", "", "", hasNote, true},
  };
  ui.overlays.open(std::move(overlay));
}

void openFolderMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "folder-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  const bool hasFolder = !ui.state.selection().folder.empty();
  overlay.items = {
    {"new-folder", "New notebook", "", "", true, false},
    {"new-note", "New note here", "", "", hasFolder, false},
    {"rename", "Rename", "", "", hasFolder, false},
    {"delete", "Delete", "", "", hasFolder, true},
  };
  ui.overlays.open(std::move(overlay));
}


}
