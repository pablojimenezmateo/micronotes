#include "app/ContextMenus.h"

#include "app/Shell.h"
#include "library/Library.h"

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
  // The pin is a tick rather than the word "Toggle": a row that says "Toggle
  // pinned" makes you open the menu to find out which way it is set, which is
  // the one thing the menu could have told you without being opened.
  overlay.items = {
    ui::menuItem("new", "New note").withKeys(ui::keysFor(ui::ActionId::NewNote)),
    ui::menuSeparator(),
    ui::menuItem("rename", "Rename")
      .withKeys(ui::keysFor(ui::ActionId::RenameNote))
      .enabledIf(hasNote),
    ui::menuItem("icon", "Set icon").enabledIf(hasNote),
    ui::menuItem("tags", "Edit tags")
      .withKeys(ui::keysFor(ui::ActionId::EditTags))
      .enabledIf(hasNote),
    ui::menuItem("pin-note", "Pinned")
      .enabledIf(hasNote)
      .ticked(hasNote && ui.state.workspace().isPinned(noteId)),
    ui::menuItem("move", "Move to notebook").enabledIf(hasNote),
    ui::menuSeparator(),
    // A note is a file, and these are the questions a reader asks about one.
    // The ids are the action names, so the menu row, the palette row and any
    // future key binding are one entry -- which is the rule the whole action
    // table exists to keep.
    ui::menuItem("show-on-disk", "Show on disk").enabledIf(hasNote),
    ui::menuItem("copy-relative-path", "Copy relative path").enabledIf(hasNote),
    ui::menuItem("copy-absolute-path", "Copy absolute path").enabledIf(hasNote),
    // With the file rows rather than in a group of its own: an export is a
    // question about the note as a document, which is what the three above it
    // are too. Ends in an ellipsis because it asks where to put it.
    ui::menuItem("export-note-pdf", "Export as PDF...").enabledIf(hasNote),
    ui::menuSeparator(),
    ui::menuItem("delete", "Delete").enabledIf(hasNote).destroys(),
  };
  // A context menu shows all of itself. The default row cap is a palette's --
  // twelve, with the rest scrolled -- and once the rules above were added the
  // note menu ran to thirteen rows, which put Delete below the fold behind a
  // scrollbar nobody expects on a menu. The window-height clamp still applies,
  // so this cannot run a menu off the screen.
  overlay.maxRows = static_cast<int>(overlay.items.size());
  ui.overlays.open(std::move(overlay));
}

void openFileMenu(UiRuntime& ui, float x, float y) {
  if(ui.sidebar.companionTarget.empty()) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "file-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  // The same four groups the note menu has, minus what only a note can do:
  // there is no icon, no tags and no pin for a file, because micronotes
  // holds nothing about a file but where it is.
  overlay.items = {
    ui::menuItem("open", "Open"),
    ui::menuSeparator(),
    ui::menuItem("rename", "Rename"),
    ui::menuItem("move", "Move to notebook"),
    ui::menuSeparator(),
    ui::menuItem("show-on-disk", "Show on disk"),
    ui::menuItem("copy-relative-path", "Copy relative path"),
    ui::menuItem("copy-absolute-path", "Copy absolute path"),
    ui::menuSeparator(),
    ui::menuItem("delete", "Delete").destroys(),
  };
  overlay.maxRows = static_cast<int>(overlay.items.size());
  ui.overlays.open(std::move(overlay));
}

void openFilesFolderMenu(UiRuntime& ui, float x, float y) {
  if(ui.sidebar.companionTarget.empty()) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "files-folder-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  // The `files` directory itself keeps its name -- it is the name that makes
  // the convention -- so its menu offers no rename. Deleting it is allowed: that
  // is a decision about the files, not about the rule.
  const bool anchor = library::isFilesDir(ui.sidebar.companionTarget);
  overlay.items = {
    ui::menuItem("new-folder", "New folder"),
    ui::menuSeparator(),
    ui::menuItem("rename", "Rename").enabledIf(!anchor),
    ui::menuSeparator(),
    ui::menuItem("show-on-disk", "Show on disk"),
    ui::menuItem("copy-relative-path", "Copy relative path"),
    ui::menuItem("copy-absolute-path", "Copy absolute path"),
    ui::menuSeparator(),
    ui::menuItem("delete", "Delete").destroys(),
  };
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
  // Whether each bulk close has anything to take. Offered greyed rather than
  // hidden when it has not: a menu whose rows come and go is a menu whose shape
  // you cannot learn, and "Close tabs to the right" being present-but-dead on
  // the last tab is itself the answer to where in the strip you are.
  //
  // Pinned tabs are spared by all of them (see `closeTabs`), so the count that
  // decides this has to skip them too -- otherwise the row on a strip of one
  // unpinned tab beside three pinned ones offers a close that does nothing.
  const auto unpinnedIn = [&tabs](std::size_t from, std::size_t to) {
    std::size_t count = 0;
    for(std::size_t i = from; i < std::min(to, tabs.size()); ++i) {
      if(!tabs[i].pinned) ++count;
    }
    return count;
  };
  const bool haveTab = index != std::string::npos;
  const std::size_t others = haveTab ? unpinnedIn(0, tabs.size()) - (pinned ? 0 : 1) : 0;
  const std::size_t toRight = haveTab ? unpinnedIn(index + 1, tabs.size()) : 0;
  const std::size_t toLeft = haveTab ? unpinnedIn(0, index) : 0;
  overlay.items = {
    ui::menuItem("close", "Close").withKeys(ui::keysFor(ui::ActionId::CloseTab)),
    ui::menuItem("close-others", "Close others")
      .withKeys(ui::keysFor(ui::ActionId::CloseOtherTabs))
      .enabledIf(others > 0),
    ui::menuItem("close-right", "Close to the right").enabledIf(toRight > 0),
    ui::menuItem("close-left", "Close to the left").enabledIf(toLeft > 0),
    ui::menuItem("close-all", "Close all").enabledIf(unpinnedIn(0, tabs.size()) > 0),
    ui::menuSeparator(),
    // Ctrl+click already pins, and nothing said so. A tab that is never
    // replaced by the next note opened is worth knowing about.
    //
    // A tick rather than a label that swaps between "Pin" and "Unpin", which
    // is the rule the note menu's own pin row follows and the reason the mark
    // column exists: a row whose *word* changes makes you open the menu to
    // find out which way the toggle is set.
    ui::menuItem("pin", "Pinned").ticked(pinned),
    ui::menuSeparator(),
    ui::menuItem("show-on-disk", "Show on disk").enabledIf(note != nullptr),
    ui::menuItem("copy-relative-path", "Copy relative path").enabledIf(note != nullptr),
    ui::menuItem("copy-absolute-path", "Copy absolute path").enabledIf(note != nullptr),
  };
  overlay.maxRows = static_cast<int>(overlay.items.size());
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
    ui::menuItem("filter", filtering ? "Clear filter" : "Filter by this tag"),
    ui::menuItem("color", "Set colour..."),
    // Offered only when there is something to undo. A "reset" that resets
    // nothing is a menu item that teaches the reader the menu is decorative.
    ui::menuItem("auto-color", "Automatic colour").enabledIf(custom),
    ui::menuSeparator(),
    // A tag has no file of its own -- it exists because notes carry it -- so
    // the only way to be rid of one was to open every note carrying it and edit
    // its front matter by hand.
    ui::menuItem("delete", "Delete tag...").destroys(),
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
    overlay.items.push_back(ui::menuItem(std::to_string(i), std::to_string(i)));
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
  overlay.items.push_back(ui::menuItem("", "None"));
  for(const auto& glyph : ui::noteGlyphs()) {
    overlay.items.push_back(ui::menuItem(std::string(glyph.id), std::string(glyph.label)));
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
    ui::menuItem("new-folder", "New notebook").withKeys(ui::keysFor(ui::ActionId::NewFolder)),
    ui::menuItem("new-note", "New note here").enabledIf(hasFolder),
    ui::menuSeparator(),
    ui::menuItem("rename", "Rename").enabledIf(hasFolder),
    ui::menuSeparator(),
    // A notebook is a directory, and the same three questions are worth asking
    // about it as about a note. The note menu has had them since they were
    // written; the tree's did not, so the path of a folder was the one thing in
    // the library a reader had to leave the app to find out.
    ui::menuItem("show-on-disk", "Show on disk"),
    ui::menuItem("copy-relative-path", "Copy relative path"),
    ui::menuItem("copy-absolute-path", "Copy absolute path"),
    // The whole notebook, its sub-notebooks included, bound into one file.
    // Offered even on the root, where it means the library.
    ui::menuItem("export-folder-pdf", "Export as PDF..."),
    ui::menuSeparator(),
    ui::menuItem("delete", "Delete").enabledIf(hasFolder).destroys(),
  };
  overlay.maxRows = static_cast<int>(overlay.items.size());
  ui.overlays.open(std::move(overlay));
}


}
