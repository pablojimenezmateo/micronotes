#include "app/ContextMenus.h"

#include "app/Shell.h"

#include "ui/Actions.h"
#include "ui/Overlay.h"
#include "ui/Glyphs.h"
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
  const auto noteId = ui.state.selection().noteId;
  const bool hasNote = !noteId.empty();
  // Four groups, ruled off from each other: what makes a note, what changes the
  // one you have, what the file underneath it is, and the one row that destroys
  // it. Without the rules the destructive row sat flush against "Copy absolute
  // path" and read as one more thing you could do to a path.
  //
  // The star is a tick rather than the word "Toggle": a row that says "Toggle
  // favorite" makes you open the menu to find out which way it is set, which is
  // the one thing the menu could have told you without being opened.
  overlay.items = {
    {"new", "New note", "", ui::keysFor(ui::ActionId::NewNote), true, false, false, false},
    {"", "", "", "", false, false, false, true},
    {"rename", "Rename", "", ui::keysFor(ui::ActionId::RenameNote), hasNote, false, false, false},
    {"icon", "Set icon", "", "", hasNote, false, false, false},
    {"tags", "Edit tags", "", ui::keysFor(ui::ActionId::EditTags), hasNote, false, false, false},
    {"favorite", "Favorite", "", "", hasNote, false, hasNote && ui.state.workspace().isFavorite(noteId), false},
    {"move", "Move to notebook", "", "", hasNote, false, false, false},
    {"", "", "", "", false, false, false, true},
    // A note is a file, and these are the questions a reader asks about one.
    // The ids are the action names, so the menu row, the palette row and any
    // future key binding are one entry -- which is the rule the whole action
    // table exists to keep.
    {"show-on-disk", "Show on disk", "", "", hasNote, false, false, false},
    {"copy-relative-path", "Copy relative path", "", "", hasNote, false, false, false},
    {"copy-absolute-path", "Copy absolute path", "", "", hasNote, false, false, false},
    {"", "", "", "", false, false, false, true},
    {"delete", "Delete", "", "", hasNote, true, false, false},
  };
  // A context menu shows all of itself. The default row cap is a palette's --
  // twelve, with the rest scrolled -- and once the rules above were added the
  // note menu ran to thirteen rows, which put Delete below the fold behind a
  // scrollbar nobody expects on a menu. The window-height clamp still applies,
  // so this cannot run a menu off the screen.
  overlay.maxRows = static_cast<int>(overlay.items.size());
  ui.overlays.open(std::move(overlay));
}

void openTabMenu(UiRuntime& ui, std::string_view noteId, float x, float y) {
  if(noteId.empty()) return;
  const auto* note = ui.state.catalog().noteById(noteId);
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "tab-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 240.0f;
  // The tab's own note names the menu, because the menu is about that tab and
  // not about whichever one is showing.
  overlay.title = note ? note->title : std::string("Missing note");
  // And travels in `value`: a result names the item chosen, and which tab it
  // was about is the other half of the answer.
  overlay.value.beginWith(std::string(noteId), false);
  const auto& tabs = ui.state.workspace().tabs;
  const auto index = ui.state.workspace().findTab(noteId);
  const bool pinned = index != std::string::npos && tabs[index].pinned;
  overlay.items = {
    {"close", "Close", "", ui::keysFor(ui::ActionId::CloseTab), true, false},
    // Ctrl+click already pins, and nothing said so. A tab that is never
    // replaced by the next note opened is worth knowing about.
    {"pin", pinned ? "Unpin" : "Pin", "", "", true, false},
    {"show-on-disk", "Show on disk", "", "", note != nullptr, false},
    {"copy-relative-path", "Copy relative path", "", "", note != nullptr, false},
    {"copy-absolute-path", "Copy absolute path", "", "", note != nullptr, false},
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

// The set of marks, not a field to type one into.
//
// It used to be a text prompt asking for "one emoji", which asked the reader to
// find a character picker, and then handed what they found to a colour emoji
// face that is one fixed 136px bitmap strike -- resampled down to the sixteen
// pixels a sidebar row gives it, and absent altogether on a machine with no
// emoji font installed, where the note simply kept its default page mark and
// the prompt appeared to have done nothing. The marks here are drawn by the
// shell at the size they are used, so a note looks the same on every machine.
void openIconPicker(UiRuntime& ui) {
  const auto& note = ui.state.openNote();
  if(note.noteId().empty()) {
    ui.status = "No note selected";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::GlyphPicker;
  overlay.id = "note-icon";
  overlay.title = "Note icon";
  overlay.hint = "Enter  choose        Esc  cancel";
  // The first cell is the absence of one, so removing an icon is a choice on
  // the same grid rather than a second gesture to be learnt.
  overlay.items.push_back({"", "None", "", "", true, false});
  for(const auto& glyph : ui::noteGlyphs()) {
    overlay.items.push_back({std::string(glyph.id), std::string(glyph.label), "", "", true, false});
  }
  overlay.current = 0;
  for(std::size_t i = 0; i < overlay.items.size(); ++i) {
    if(overlay.items[i].id != note.metadata().icon) continue;
    overlay.current = static_cast<int>(i);
    break;
  }
  overlay.highlighted = overlay.current;
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
    {"new-folder", "New notebook", "", ui::keysFor(ui::ActionId::NewFolder), true, false, false, false},
    {"new-note", "New note here", "", "", hasFolder, false, false, false},
    {"", "", "", "", false, false, false, true},
    {"rename", "Rename", "", "", hasFolder, false, false, false},
    {"", "", "", "", false, false, false, true},
    {"delete", "Delete", "", "", hasFolder, true, false, false},
  };
  overlay.maxRows = static_cast<int>(overlay.items.size());
  ui.overlays.open(std::move(overlay));
}


}
