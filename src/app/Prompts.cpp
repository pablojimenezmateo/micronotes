#include "app/Prompts.h"

#include "app/Notes.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "core/platform/PathUtils.h"
#include "ui/Actions.h"
#include "ui/Overlay.h"
#include "library/Metadata.h"
#include "core/platform/PathUtils.h"

#include <string>
#include <utility>
#include <vector>

namespace micronotes::app {
namespace {

using micronotes::library::joinTags;
using micronotes::library::splitTags;

// Whether there is a notebook to act on, which for the root there is not: it
// is the library, and renaming or deleting it is not a notebook operation.
//
// One function because the answer and the sentence that reports it were written
// out three times -- and the confirm and the delete behind it each checked
// separately, so the two could have disagreed about what "no notebook" means.
bool hasSelectedFolder(UiRuntime& ui, const char* verb) {
  if(!ui.state.selection().folder.empty()) return true;
  ui.status = std::string("Root notebook cannot be ") + verb;
  return false;
}

}

void beginTagEdit(UiRuntime& ui) {
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  const auto& note = ui.state.openNote();
  if(note.noteId().empty()) {
    ui.status = "Select a note before editing tags";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "tags";
  overlay.title = "Tags for \"" + std::string(ui.state.selectedTitle()) + "\"";
  overlay.value.beginWith(joinTags(note.metadata().tags));
  overlay.placeholder = "space separated";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

void saveTags(UiRuntime& ui) {
  if(ui.state.updateSelectedTags(splitTags(ui.fields.tag.text()), ui.editor.text())) {
    ui.focus = FocusArea::Editor;
    ui.status = "Saved tags";
  } else {
    ui.status = "No selected note for tags";
  }
}

void beginNoteCreate(UiRuntime& ui, std::string value, std::string hint) {
  if(!ui.state.catalog().isOpen()) {
    ui.status = "Start with --library <path> before creating notes";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "new-note-name";
  overlay.title = "New note";
  overlay.value.beginWith(std::move(value));
  overlay.placeholder = "Note title";
  overlay.hint = std::move(hint);
  ui.overlays.open(std::move(overlay));
}

void beginNoteCreate(UiRuntime& ui) {
  beginNoteCreate(ui, "Untitled", "Enter to create, Esc to cancel");
}

void saveNoteCreate(UiRuntime& ui, const std::string& asked) {
  const std::string title = asked.empty() ? "Untitled" : asked;
  // A note is a file, so two notes in one notebook cannot both be called
  // `TODO`. The rename prompt numbers the second one and says so, because by
  // then the note exists and refusing would leave it under a name nobody
  // chose; here nothing exists yet, so the reader is asked again rather than
  // handed a `TODO-2` they did not type.
  const auto& catalog = ui.state.catalog();
  const auto folder = ui.state.selection().folder;
  if(catalog.uniqueTitle(title, folder) != title) {
    beginNoteCreate(ui, title, "A note called \"" + title + "\" is already here");
    ui.status = "That name is taken";
    return;
  }
  createNote(ui, title);
}

void beginFolderCreate(UiRuntime& ui) {
  if(!ui.state.catalog().isOpen()) {
    ui.status = "Open a library before creating notebooks";
    return;
  }
  ui.sidebar.creatingFolder = true;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "folder-name";
  overlay.title = "New notebook";
  overlay.value.beginWith("Notebook");
  overlay.placeholder = "Notebook name";
  overlay.hint = "Enter to create, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

void beginFolderRename(UiRuntime& ui) {
  if(!hasSelectedFolder(ui, "renamed")) return;
  ui.sidebar.creatingFolder = false;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "folder-name";
  overlay.title = "Rename notebook";
  overlay.value.beginWith(ui.state.selection().folder.generic_string());
  overlay.placeholder = "Notebook name";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

void saveFolderRename(UiRuntime& ui) {
  if(ui.fields.folderRename.empty()) {
    ui.status = "Notebook name is required";
    return;
  }
  const bool creating = ui.sidebar.creatingFolder;
  const bool saved = creating ? ui.state.createFolder(ui.fields.folderRename.text()) : ui.state.renameSelectedFolder(ui.fields.folderRename.text());
  if(saved) {
    ui.sidebar.creatingFolder = false;
    ui.focus = FocusArea::Folders;
    ui.status = creating ? "Created notebook" : "Saved notebook";
  } else {
    ui.status = "Notebook change failed";
  }
}

void deleteSelected(UiRuntime& ui) {
  invalidateWikiNotes(ui);
  if(ui.state.deleteSelectedNote()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
    selectNoteAt(ui, 0);
    ui.status = "Deleted note";
  } else {
    ui.status = "Delete failed";
  }
}

void deleteSelectedFolder(UiRuntime& ui) {
  if(!hasSelectedFolder(ui, "deleted")) return;
  if(ui.state.deleteSelectedFolder()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
    ui.status = "Deleted notebook";
  } else {
    ui.status = "Notebook delete failed";
  }
}

// The palette is a view of ui::actionSpecs(), not a second list beside it.
// It used to be its own table, and the rule written above it -- that an action
// reachable only by shortcut belongs in both -- was enforced by nobody.

void openCommandPalette(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "command-palette";
  overlay.title = "Commands";
  overlay.filterable = true;
  overlay.placeholder = "Type a command";
  overlay.hint = "Enter run   Esc cancel";
  overlay.width = 460.0f;
  const bool hasNote = !ui.state.selection().noteId.empty();
  for(const auto& spec : ui::actionSpecs()) {
    if(!spec.inPalette) continue;
    // Commands that need something selected are listed but refused, rather
    // than hidden: a palette that changes shape is a palette you cannot learn.
    overlay.items.push_back({std::string(spec.name), std::string(spec.label), "",
                             ui::acceleratorText(spec), !spec.needsNote || hasNote, false});
  }
  ui.overlays.open(std::move(overlay));
}

// One list of every note in the library, reused by "go to note" and by anything
// that has to name a target note. `id` says which, so the result knows what it
// is answering.
void openNotePalette(UiRuntime& ui, std::string overlayId, std::string title) {
  ui::Overlay overlay;
  overlay.id = std::move(overlayId);
  overlay.title = std::move(title);
  overlay.filterable = true;
  overlay.placeholder = "Type a note title";
  overlay.hint = "Enter open   Esc cancel";
  overlay.width = 460.0f;
  const auto root = ui.state.catalog().root();
  for(const auto& note : ui.state.catalog().notes()) {
    const auto folder = note.folder.generic_string();
    // The title alone: an icon is a drawn mark now, and its id ("bookmark")
    // pasted in front of a note's name is a word the reader never chose.
    overlay.items.push_back({note.id, note.title,
                             folder.empty() ? root.filename().generic_string() : folder,
                             "", true, false});
  }
  if(overlay.items.empty()) {
    ui.status = "No notes to jump to";
    return;
  }
  ui.overlays.open(std::move(overlay));
}

void openFolderPalette(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "move-note-folder";
  overlay.title = "Move note to";
  overlay.filterable = true;
  overlay.placeholder = "Type a notebook name";
  overlay.hint = "Enter move   Esc cancel";
  overlay.width = 420.0f;
  const auto rootLabel = ui.state.catalog().root().filename().generic_string();
  for(const auto& folder : ui.state.catalog().folders()) {
    overlay.items.push_back({folder.path.generic_string().empty() ? "/" : folder.path.generic_string(),
                             folder.path.empty() ? rootLabel : folder.path.generic_string(),
                             "", std::to_string(folder.noteCount), true, false});
  }
  ui.overlays.open(std::move(overlay));
}

void openTrashPalette(UiRuntime& ui) {
  const auto entries = ui.state.catalog().trashEntries();
  if(entries.empty()) {
    ui.status = "Trash is empty";
    return;
  }
  ui::Overlay overlay;
  overlay.id = "restore-trash";
  overlay.title = "Restore from trash";
  overlay.filterable = true;
  overlay.placeholder = "Type a name";
  overlay.hint = "Enter restore   Esc cancel";
  overlay.width = 460.0f;
  for(const auto& entry : entries) {
    overlay.items.push_back({entry.name, entry.title,
                             entry.originalRelative.parent_path().generic_string(), entry.deletedAt, true, false});
  }
  ui.overlays.open(std::move(overlay));
}


// `~` for the home directory, as everything else that prints a path does.
void openLibraryPrompt(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "settings-library";
  overlay.title = "Library folder";
  overlay.value.beginWith(ui.state.catalog().isOpen() ? platform::displayPath(ui.state.catalog().root()) : std::string {});
  overlay.placeholder = "~/Notes";
  overlay.hint = "Enter open   Esc cancel   a folder that is not there is created";
  overlay.width = 520.0f;
  ui.overlays.open(std::move(overlay));
}

void openDeleteNoteConfirm(UiRuntime& ui) {
  const auto& note = ui.state.openNote();
  if(note.noteId().empty()) {
    ui.status = "Select a note before deleting";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::Confirm;
  overlay.id = "delete-note";
  overlay.title = "Delete \"" + std::string(ui.state.selectedTitle()) + "\"?";
  overlay.hint = "This cannot be undone.";
  overlay.confirmLabel = "Delete";
  overlay.width = 380.0f;
  ui.overlays.open(std::move(overlay));
}

void openDeleteFolderConfirm(UiRuntime& ui) {
  if(!hasSelectedFolder(ui, "deleted")) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::Confirm;
  overlay.id = "delete-folder";
  overlay.title = "Delete notebook \"" + ui.state.selection().folder.generic_string() + "\"?";
  overlay.hint = "Every note inside it is deleted too. This cannot be undone.";
  overlay.confirmLabel = "Delete";
  overlay.width = 420.0f;
  ui.overlays.open(std::move(overlay));
}

}
