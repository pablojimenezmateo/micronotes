#include "app/ContextMenus.h"

#include "app/Shell.h"

#include "ui/Actions.h"
#include "ui/Overlay.h"
#include "ui/TagColors.h"

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

void openTagMenu(UiRuntime& ui, std::string_view tag, float x, float y) {
  if(tag.empty()) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "tag-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 240.0f;
  // The tag itself is the title. A menu of three items about "this tag" needs
  // to say which tag, and it was reached from a 7px dot -- so this is often the
  // first place its name is written out at all.
  overlay.title = std::string(tag);
  // And carried in `value`, which is what a result reports back alongside the
  // item chosen: the tag is half the answer and the item id is the other half.
  overlay.value.beginWith(std::string(tag), false);
  const bool filtering = ui.state.selection().tag == tag;
  const bool custom = ui.state.workspace().tagColors.picked(tag);
  overlay.items = {
    {"filter", filtering ? "Clear filter" : "Filter by this tag", "", "", true, false},
    {"color", "Set colour...", "", "", true, false},
    // Offered only when there is something to undo. A "reset" that resets
    // nothing is a menu item that teaches the reader the menu is decorative.
    {"auto-color", "Automatic colour", "", "", custom, false},
  };
  ui.overlays.open(std::move(overlay));
}

void openTagColorPicker(UiRuntime& ui, std::string tag) {
  if(tag.empty()) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::ColorPicker;
  overlay.id = "tag-color";
  overlay.title = tag;
  overlay.hint = "Enter  choose        Esc  cancel";
  // The whole palette, in palette order, with the item's id being the swatch
  // index. Nothing to filter and nothing to name: the swatch *is* the label,
  // which is why this is a picker rather than a list of colour names.
  overlay.items.reserve(ui::kTagSwatchCount);
  for(int i = 0; i < ui::kTagSwatchCount; ++i) {
    overlay.items.push_back({std::to_string(i), std::to_string(i), "", "", true, false});
  }
  const auto& colors = ui.state.workspace().tagColors;
  // The swatch in force, whether it was chosen or derived -- the reader wants
  // to know what the tag looks like now, and "derived" is not a different
  // answer to that question. The `Automatic colour` item on the menu behind
  // this is what distinguishes the two.
  overlay.current = colors.swatchOf(tag);
  overlay.highlighted = overlay.current;
  // Carried through the commit, because the result only reports which item was
  // chosen and the tag is the other half of the answer.
  overlay.value.beginWith(std::move(tag), false);
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
