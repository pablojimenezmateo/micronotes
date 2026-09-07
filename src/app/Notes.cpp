#include "app/Notes.h"

#include "app/ContextMenus.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace micronotes::app {

namespace {

// A note opens at the top of itself, in whichever pane is showing it. The
// reading pane reset its scroll here and the live page did not, so opening a
// note after scrolling in another one landed you at the top in one pane and
// part-way down in the other -- the two are the same renderer, and this is the
// last piece of state that did not know it.
void resetPageScroll(UiRuntime& ui) {
  ui.livePage.setScroll(0);
  ui.readingPage.setScroll(0);
}

// Puts the selected note into the editor, preferring the crash-recovery copy
// when it differs from what is on disk.
//
// The one path into the editor buffer. There were three: two identical copies
// inside `selectNoteAt` and `selectNoteById`, and a third in
// `loadSelectedIntoEditor` that had drifted -- it did not consult the recovery
// store at all. That third one is the *startup* path, which is the one case the
// recovery store exists for. So a crash with unsaved text handed the draft back
// only if you happened to click the note's name again afterwards; open the app,
// see the note it had left open, type one character, and 1.2 seconds later the
// autosave retired the recovery file and the draft was gone. Two hundred lines
// of careful durable-write machinery, defeated by the buffer being filled
// through the wrong door.
void loadSelectedBuffer(UiRuntime& ui, bool resetView) {
  const auto note = ui.state.readSelectedNote();
  if(!note) return;
  const std::string noteId = ui.state.selection().noteId;
  ui.loadedNoteId = noteId;
  const auto recovered = ui.state.selectedRecoveryBody();
  const bool unsaved = recovered && *recovered != note->body;
  ui.editor.setText(unsaved ? *recovered : note->body);
  // Marked dirty so the recovered text is treated as unsaved work rather than
  // as the file's contents -- which is what makes the next autosave commit it.
  if(unsaved) ui.editor.markDirty();
  if(resetView) {
    ui.editorScroll = 0;
    resetPageScroll(ui);
    ui.revealEditorCursor = false;
  }
  const std::string title(ui.state.selectedTitle());
  ui.status = unsaved ? "Recovered unsaved " + title : "Loaded " + title;
  ui.state.noteOpened(noteId);
}

}

void selectNoteAt(UiRuntime& ui, int index) {
  auto notes = ui.state.currentNotes();
  if(notes.empty()) return;
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  index = std::clamp(index, 0, static_cast<int>(notes.size()) - 1);
  ui.noteCursor = index;
  ui.state.selectNote(notes[static_cast<std::size_t>(index)].id);
  loadSelectedBuffer(ui, /*resetView=*/true);
}

void selectTag(UiRuntime& ui, const std::string& tag) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  // Choosing the tag already in force clears it, which is the affordance the
  // empty state has been promising ("click the tag again to clear the filter")
  // since before there was anything to click: a tag row and a note row look and
  // behave alike everywhere else, so a second click on the row you are already
  // filtered by should not be a no-op. Reachable from the right panel's Tags
  // view, which keeps listing the tags while the filter runs.
  if(!tag.empty() && ui.state.selection().tag == tag) {
    clearTagFilter(ui);
    return;
  }
  ui.state.selectTag(tag);
  selectNoteAt(ui, 0);
}

void setTagColor(UiRuntime& ui, std::string_view tag, std::string_view swatch) {
  if(tag.empty()) return;
  int index = 0;
  const auto* first = swatch.data();
  const auto [ptr, ec] = std::from_chars(first, first + swatch.size(), index);
  // Refused rather than defaulted to the first swatch. The id comes from a
  // picker that built it, so a value that will not parse means the two have
  // drifted apart -- and silently painting the tag blue would hide that.
  if(ec != std::errc {} || ptr != first + swatch.size()) return;
  ui.state.workspace().tagColors.set(std::string(tag), index);
  ui.status = "Coloured " + std::string(tag);
}

bool handleTagOverlayResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId == "tag-menu") {
    if(result.itemId == "filter") selectTag(ui, result.value);
    else if(result.itemId == "color") openTagColorPicker(ui, result.value);
    else if(result.itemId == "auto-color") clearTagColor(ui, result.value);
    return true;
  }
  if(result.overlayId == "tag-color") {
    setTagColor(ui, result.value, result.itemId);
    return true;
  }
  return false;
}

void clearTagColor(UiRuntime& ui, std::string_view tag) {
  if(tag.empty()) return;
  ui.state.workspace().tagColors.clear(tag);
  ui.status = std::string(tag) + " back to its automatic colour";
}

const library::NoteListItem* noteAtLinkTarget(UiRuntime& ui, std::string_view relative) {
  if(relative.empty() || !ui.state.hasLibrary()) return nullptr;
  // A URL is somebody else's business, and so is an absolute path: a link out
  // of the library is not a link to a note in it.
  if(ui::isRemoteTarget(relative)) return nullptr;
  const std::string decoded = ui::decodeLinkTarget(relative);
  if(decoded.empty() || decoded.front() == '/') return nullptr;

  const auto root = ui.state.libraryRoot();
  // Two bases, most specific first. A relative path in a note means "relative
  // to this note", which is what every Markdown renderer does and what a link
  // written by hand inside a subfolder assumes; resolving against the root as
  // well is what keeps working the links that were written the other way, and
  // there is no way to tell those apart other than to try both.
  std::filesystem::path bases[2] = {root, root};
  if(const auto* open = ui.state.noteById(ui.state.selection().noteId)) {
    bases[0] = root / open->folder;
  }
  for(const auto& base : bases) {
    std::error_code ec;
    // `weakly_canonical` rather than `canonical`: the target need not exist for
    // the arithmetic to be right, and a link to a note that has been deleted
    // should come back as "not a note" rather than throwing.
    const auto candidate = std::filesystem::weakly_canonical(base / decoded, ec);
    if(ec) continue;
    // Nothing outside the library, however many `..` the target spends getting
    // there. This is the one check that has to hold whatever the target says:
    // a note's text is not a licence to open arbitrary files, and the same rule
    // is why `attachments::AttachmentService` refuses an escaping path.
    const auto inside = std::filesystem::weakly_canonical(root, ec);
    if(ec || candidate.native().rfind(inside.native(), 0) != 0) continue;
    // Against the note list rather than the disk. The list is what the rest of
    // the app means by "a note", so a `.md` file the index has not taken up --
    // one inside a hidden folder, say -- stays the desktop's job, and the
    // answer costs no syscall.
    for(const auto& note : ui.state.allNotes()) {
      if(note.path == candidate) return &note;
    }
  }
  return nullptr;
}

bool clearTagFilter(UiRuntime& ui) {
  if(ui.state.selection().tag.empty()) return false;
  // The note stays open. Leaving the filter is a question about what the
  // sidebar lists, not about what is being read -- closing the note as well
  // would make going back cost the reader their place.
  const auto note = ui.state.findNote(ui.state.selection().noteId);
  ui.state.selectTag({});
  // Onto the folder the open note lives in, with the tree opened to it, so the
  // list comes back showing where you ended up rather than at the root having
  // forgotten the last five minutes. `selectTag` cleared the folder on the way
  // in, which is why there is something to restore.
  showFolder(ui, note ? note->folder : std::filesystem::path {});
  ui.status = "Cleared tag filter";
  return true;
}

void showFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  ui.state.selectFolder(folder);
  ui.tree.reveal(folder);
}

void selectNoteById(UiRuntime& ui, const std::string& noteId, ui::TabPolicy policy) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectNote(noteId, policy);
  loadSelectedBuffer(ui, /*resetView=*/true);
}

void loadSelectedIntoEditor(UiRuntime& ui) {
  ui.clearBlockSelection();
  // A dirty buffer for the note already showing is the newest copy of it, so it
  // stays. Anything else -- a different note, or the same one with nothing
  // unsaved -- is read.
  if(ui.loadedNoteId == ui.state.selection().noteId && ui.editor.dirty()) return;
  // The view is kept: the callers are a rename and opening a library, and in
  // neither case has the reader's position in the note moved.
  loadSelectedBuffer(ui, /*resetView=*/false);
}

bool reloadSelectedIfChangedOnDisk(UiRuntime& ui) {
  if(!ui.state.hasLibrary() || ui.state.selection().noteId.empty()) return false;
  switch(ui.state.selectedNoteDiskState()) {
    case ui::DiskState::Agrees:
      return false;
    case ui::DiskState::Vanished:
      // The buffer is now the only copy there is, so it is kept and the next
      // save writes the file back. Deleting the note out from under a reader
      // because something else deleted the file is the wrong way to be
      // consistent.
      ui.status = "The file for this note is gone -- saving writes it back";
      return false;
    case ui::DiskState::Changed:
    case ui::DiskState::Unknown:
      break;
  }
  if(ui.editor.dirty()) {
    // Not overwritten. The save path resolves this, and resolves it without
    // dropping either version: the text that appeared on disk is filed beside
    // the note as a note of its own.
    ui.status = "Changed on disk -- saving keeps both versions";
    return false;
  }
  if(!ui.state.reloadSelectedNote()) return false;
  invalidateWikiNotes(ui);
  // The reader's place in the note is kept rather than reset. The bytes moved,
  // so it is an approximation -- but jumping a reader to the top of a note
  // because a sync daemon touched the file is worse than an approximate
  // position, and the layout clamps whatever no longer fits.
  loadSelectedBuffer(ui, /*resetView=*/false);
  ui.status = "Reloaded " + std::string(ui.state.selectedTitle()) + " from disk";
  return true;
}

void rescanLibraryAfterExternalChange(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) return;
  invalidateWikiNotes(ui);
  ui.state.refreshLibrary();
  reloadSelectedIfChangedOnDisk(ui);
}

bool applyWatchedChanges(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) return false;
  const bool rescan = ui.watcher.takeRescanRequest();
  const auto paths = ui.watcher.takeChanges();
  if(rescan) {
    // Drained first, above: the paths collected alongside a rescan request are
    // covered by the rescan, and leaving them queued would spend a second pass
    // re-indexing files the refresh has just read.
    perf::addCounter(perf::CounterId::WatcherRescans);
    rescanLibraryAfterExternalChange(ui);
    return true;
  }
  if(paths.empty()) return false;

  bool listChanged = false;
  bool touchedAnything = false;
  for(const auto& path : paths) {
    // Only notes. A library holds whatever the user puts in it, and an image
    // dropped next to a note is not a change to the library's contents.
    if(path.extension() != ".md") continue;
    perf::addCounter(perf::CounterId::WatcherPathsApplied);
    listChanged = ui.state.refreshNoteFile(path) || listChanged;
    touchedAnything = true;
  }
  if(!touchedAnything) return false;
  if(listChanged) {
    ui.state.invalidateNoteList();
    invalidateWikiNotes(ui);
  }
  // Last, and only for the note on screen: the index is now right about every
  // file that moved, and this is the one whose bytes a person is looking at.
  reloadSelectedIfChangedOnDisk(ui);
  return true;
}

void createNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Start with --library <path> before creating notes";
    return;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  const auto folder = ui.state.selection().folder;
  invalidateWikiNotes(ui);
  // An empty body, not a `# Untitled` heading. The page draws the note's name
  // above its first block, so seeding one only put the name on screen twice and
  // left the caret on the second copy; the empty page prompts for a first line
  // instead, which is where the caret already is.
  if(auto created = ui.state.createNote("Untitled", folder, "")) {
    ui.loadedNoteId = created->id;
    ui.editor.setText("");
    ui.editorScroll = 0;
    resetPageScroll(ui);
    ui.revealEditorCursor = true;
    ui.focus = FocusArea::Editor;
    ui.status = "Created " + created->title;
  }
}

void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  const auto previousFolder = ui.state.selection().folder;
  ui.state.selectFolder(folder);
  createNote(ui);
  if(ui.state.selection().noteId.empty()) ui.state.selectFolder(previousFolder);
}

bool saveCurrent(UiRuntime& ui, bool quiet) {
  if(!ui.state.hasLibrary()) {
    if(!quiet) ui.status = "No library open";
    return false;
  }
  if(ui.state.selection().noteId.empty()) {
    createNote(ui);
  }
  const auto result = ui.state.saveSelectedNote(ui.editor.text());
  if(!result.ok) {
    ui.status = quiet ? "Autosave failed" : "Save failed";
    return false;
  }
  ui.editor.markSaved();
  if(!result.conflictCopy.empty()) {
    // Never quiet, however the save was triggered. Something else had rewritten
    // the note and that version has been kept as a note of its own; a person
    // who is not told will not find it, and an autosave the user did not ask
    // for is exactly when they most need to hear it.
    ui.status = "Saved -- the changed file was kept as " + result.conflictCopy;
    invalidateWikiNotes(ui);
  } else if(!quiet) {
    // Named by the library, not by the first line of the buffer. The title
    // lives in the note's header, and a note whose body happens to be empty is
    // still not called "Untitled".
    ui.status = "Saved " + std::string(ui.state.selectedTitle());
  }
  return true;
}

}
