#include "app/Notes.h"

#include "app/Prompts.h"

#include "doc/LinkTarget.h"

#include "app/ContextMenus.h"
#include "app/Desktop.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"

#include <algorithm>
#include <filesystem>
#include <charconv>
#include <string>
#include <vector>

namespace micronotes::app {

namespace {

// A note opens at the top of itself, in all three panes. The raw pane's rebase
// joined the other two here rather than being spelled beside each call: it is
// the third surface showing the same note, and the two callers that reset a
// view both wanted all three.
void resetPageScroll(UiRuntime& ui) {
  ui.livePage.setScroll(0);
  ui.readingPage.setScroll(0);
  ui.raw.list.rebase();
}

// Puts the three panes' offsets away on the tab for the note being left, and
// takes them back out for the note being arrived at. See `NoteTab`'s scroll
// fields for why they live on the tab.
//
// By note id rather than by the active tab index, because the two do not move
// at the same moment: `AppState::selectNote` opens the tab and moves the
// selection together, so by the time the buffer is loaded the active tab is
// already the new one and the tab the offsets belong to is only findable by the
// id the editor was holding.
void rememberPageScroll(UiRuntime& ui) {
  auto& workspace = ui.state.editWorkspace();
  const auto index = workspace.findTab(ui.loadedNoteId);
  if(index == std::string::npos) return;
  workspace.tabs[index].liveScroll = ui.livePage.scroll();
  workspace.tabs[index].readingScroll = ui.readingPage.scroll();
  workspace.tabs[index].rawScroll = ui.raw.list.scroll();
}

void restorePageScroll(UiRuntime& ui) {
  const auto& workspace = ui.state.workspace();
  const auto index = workspace.findTab(ui.state.selection().noteId);
  if(index == std::string::npos) {
    // A note showing without a tab of its own -- there is no such state today,
    // but a note with nowhere to have kept an offset opens at its top, which is
    // what it did before any of this existed.
    resetPageScroll(ui);
    return;
  }
  const auto& tab = workspace.tabs[index];
  // `restore` rather than `setScroll`: the switch happens while each pane's
  // ceiling still belongs to the note being left, so clamping now would lose
  // the place on any note longer than that one. The next layout clamps it,
  // which is also what keeps a place remembered while the note grew shorter
  // elsewhere from landing past its end.
  ui.livePage.restoreScroll(tab.liveScroll);
  ui.readingPage.restoreScroll(tab.readingScroll);
  ui.raw.list.restore(tab.rawScroll);
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
void loadSelectedBuffer(UiRuntime& ui) {
  const auto note = ui.state.openNote().read();
  if(!note) return;
  const std::string noteId = ui.state.selection().noteId;
  // Whether this is a move to a different note at all. A rename and a reload
  // come through here for the note already open, and neither has moved the
  // reader -- so neither may touch the view.
  const bool moved = ui.loadedNoteId != noteId;
  if(moved) rememberPageScroll(ui);
  ui.loadedNoteId = noteId;
  const auto recovered = ui.state.openNote().recoveryBody();
  const bool unsaved = recovered && *recovered != note->body;
  ui.editor.setText(unsaved ? *recovered : note->body);
  // Marked dirty so the recovered text is treated as unsaved work rather than
  // as the file's contents -- which is what makes the next autosave commit it.
  if(unsaved) ui.editor.markDirty();
  // Only on a move, and that replaces the `resetView` flag the five callers
  // used to pass. It meant "back to the top", which the sidebar and the
  // keyboard cursor asked for and the tab strip did not -- so switching by tab
  // inherited the offset of whatever note it was leaving, and switching by the
  // sidebar threw the note's own place away. Both are the same question now,
  // and the answer to it is on the tab.
  //
  // Gating on the move rather than on a flag is also what stops a *re*-load of
  // the note already showing -- a rename, a rescan, the same row clicked twice
  // -- from dragging the reader back to wherever they were when they arrived.
  // Three of those fire in a row on a single sidebar click.
  if(moved) {
    restorePageScroll(ui);
    ui.revealEditorCursor = false;
  }
  const std::string title(ui.state.selectedTitle());
  ui.status = unsaved ? "Recovered unsaved " + title : "Loaded " + title;
  ui.state.editWorkspace().noteOpened(noteId);
}

}

void selectNoteAt(UiRuntime& ui, int index) {
  auto notes = ui.state.currentNotes();
  if(notes.empty()) return;
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  index = std::clamp(index, 0, static_cast<int>(notes.size()) - 1);
  ui.state.selectNote(notes[static_cast<std::size_t>(index)].id);
  loadSelectedBuffer(ui);
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
  ui.state.editWorkspace().tagColors.set(std::string(tag), index);
  ui.status = "Coloured " + std::string(tag);
}

bool handleTagOverlayResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId == "tag-menu") {
    if(result.itemId == "filter") selectTag(ui, result.value);
    else if(result.itemId == "color") openTagColorPicker(ui, result.value);
    else if(result.itemId == "auto-color") clearTagColor(ui, result.value);
    else if(result.itemId == "delete") openDeleteTagConfirm(ui, result.value);
    return true;
  }
  if(result.overlayId == "delete-tag") {
    deleteTag(ui, result.value);
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
  ui.state.editWorkspace().tagColors.clear(tag);
  ui.status = std::string(tag) + " back to its automatic colour";
}

LibraryPaths libraryPathsFor(const UiRuntime& ui, const std::filesystem::path& absolute) {
  if(absolute.empty()) return {};
  LibraryPaths paths;
  const auto path = absolute.lexically_normal();
  paths.absolute = path.string();
  // `lexically_relative` rather than trimming a string prefix, so a root
  // spelled with a trailing slash, a `.` or a `..` in it still gives the same
  // answer. Generic separators, because a relative path is the spelling that
  // goes into a note or a message to somebody else.
  const auto text = path.lexically_relative(ui.state.catalog().root().lexically_normal())
                      .generic_string();
  // Empty or climbing out with ".." mean it is not under the root. Left empty
  // rather than falling back to the absolute path: a "relative path" that is
  // absolute is the wrong answer given confidently. "." is the root itself,
  // which *is* under the root and is spelled as itself.
  if(!text.empty() && !text.starts_with("..")) paths.relative = text;
  return paths;
}

LibraryPaths notePathsFor(const UiRuntime& ui, std::string_view noteId) {
  // The note named, or the one on the page. Off the note list rather than off
  // `openNote()`, so a right click on a sidebar row or a tab answers about
  // *that* one and not about whatever happens to be open.
  const auto* note = ui.state.catalog().noteById(noteId.empty() ? ui.state.selection().noteId : noteId);
  if(!note) return {};
  return libraryPathsFor(ui, note->path);
}

LibraryPaths folderPathsFor(const UiRuntime& ui, const std::filesystem::path& folder) {
  if(!ui.state.catalog().isOpen()) return {};
  const auto& root = ui.state.catalog().root();
  return libraryPathsFor(ui, folder.empty() ? root : root / folder);
}

namespace {

// The three path commands, once, over whatever was named. `missing` is what to
// say when there is nothing to name, and `outside` when the thing is real and
// sits outside the library -- the two sentences are the only part of this that
// differs between a note and a notebook.
bool carryOutPathCommand(UiRuntime& ui, std::string_view command, const LibraryPaths& paths,
                         const char* missing, const char* outside) {
  const bool onDisk = command == "show-on-disk";
  const bool relative = command == "copy-relative-path";
  const bool absolute = command == "copy-absolute-path";
  if(!onDisk && !relative && !absolute) return false;

  if(paths.absolute.empty()) {
    ui.status = missing;
    return true;
  }
  const std::filesystem::path path = paths.absolute;
  if(onDisk) {
    ui.status = revealInFileManager(path) ? "Showing " + path.filename().string() + " on disk"
                                          : "Could not open the file manager";
    return true;
  }
  if(relative && paths.relative.empty()) {
    ui.status = outside;
    return true;
  }
  const std::string& text = relative ? paths.relative : paths.absolute;
  ui.status = setClipboardText(text) ? "Copied " + text : "Clipboard unavailable";
  return true;
}

}

bool handleNotePathCommand(UiRuntime& ui, std::string_view command, std::string_view noteId) {
  return carryOutPathCommand(ui, command, notePathsFor(ui, noteId), "No note to locate",
                             "That note is not inside the library");
}

bool handleFolderPathCommand(UiRuntime& ui, std::string_view command,
                             const std::filesystem::path& folder) {
  return carryOutPathCommand(ui, command, folderPathsFor(ui, folder), "No notebook to locate",
                             "That notebook is not inside the library");
}

LibraryPaths companionPathsFor(const UiRuntime& ui, const std::filesystem::path& relative) {
  if(!ui.state.catalog().isOpen() || relative.empty()) return {};
  return libraryPathsFor(ui, ui.state.catalog().root() / relative);
}

bool handleCompanionPathCommand(UiRuntime& ui, std::string_view command,
                                const std::filesystem::path& relative) {
  return carryOutPathCommand(ui, command, companionPathsFor(ui, relative), "No file to locate",
                             "That file is not inside the library");
}

bool openCompanion(UiRuntime& ui, const std::filesystem::path& relative) {
  if(!ui.state.catalog().isOpen() || relative.empty()) return false;
  const auto path = ui.state.catalog().root() / relative;
  const auto name = relative.filename().generic_string();
  std::error_code ec;
  if(!std::filesystem::exists(path, ec)) {
    ui.status = name + " is no longer on disk";
    return false;
  }
  const bool opened = ui.launcher ? ui.launcher(path) : openWithDesktop(path.string());
  ui.status = opened ? "Opened " + name : "Could not open " + name;
  return opened;
}

const library::NoteListItem* noteAtLinkTarget(UiRuntime& ui, std::string_view relative) {
  if(relative.empty() || !ui.state.catalog().isOpen()) return nullptr;
  // A URL is somebody else's business, and so is an absolute path: a link out
  // of the library is not a link to a note in it.
  if(doc::isRemoteTarget(relative)) return nullptr;
  const std::string decoded = doc::decodeLinkTarget(relative);
  if(decoded.empty() || decoded.front() == '/') return nullptr;

  const auto root = ui.state.catalog().root();
  // Two bases, most specific first. A relative path in a note means "relative
  // to this note", which is what every Markdown renderer does and what a link
  // written by hand inside a subfolder assumes; resolving against the root as
  // well is what keeps working the links that were written the other way, and
  // there is no way to tell those apart other than to try both.
  std::filesystem::path bases[2] = {root, root};
  if(const auto* open = ui.state.catalog().noteById(ui.state.selection().noteId)) {
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
    for(const auto& note : ui.state.catalog().notes()) {
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
  const auto note = ui.state.catalog().findNote(ui.state.selection().noteId);
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
  ui.sidebar.tree.reveal(folder);
}

void selectNoteById(UiRuntime& ui, const std::string& noteId, ui::TabPolicy policy) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectNote(noteId, policy);
  loadSelectedBuffer(ui);
}

void loadSelectedIntoEditor(UiRuntime& ui) {
  ui.blockSelection.clear();
  // A dirty buffer for the note already showing is the newest copy of it, so it
  // stays. Anything else -- a different note, or the same one with nothing
  // unsaved -- is read.
  if(ui.loadedNoteId == ui.state.selection().noteId && ui.editor.dirty()) return;
  loadSelectedBuffer(ui);
}

bool reloadSelectedIfChangedOnDisk(UiRuntime& ui) {
  if(!ui.state.catalog().isOpen() || ui.state.selection().noteId.empty()) return false;
  switch(ui.state.openNote().diskState()) {
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
  loadSelectedBuffer(ui);
  ui.status = "Reloaded " + std::string(ui.state.selectedTitle()) + " from disk";
  return true;
}

void rescanLibraryAfterExternalChange(UiRuntime& ui) {
  if(!ui.state.catalog().isOpen()) return;
  invalidateWikiNotes(ui);
  ui.state.refreshLibrary();
  reloadSelectedIfChangedOnDisk(ui);
}

bool applyWatchedChanges(UiRuntime& ui) {
  if(!ui.state.catalog().isOpen()) return false;
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
  // The `<notebook>/files` directories something changed under, each once. A
  // change there is a change to what the tree lists, but not to any note, so it
  // costs one small walk of that directory rather than a refresh of the index.
  std::vector<std::filesystem::path> filesDirs;
  const auto& root = ui.state.catalog().root();
  for(const auto& path : paths) {
    if(const auto filesDir = library::filesRootOf(path.lexically_relative(root)); !filesDir.empty()) {
      if(std::find(filesDirs.begin(), filesDirs.end(), filesDir) == filesDirs.end()) {
        filesDirs.push_back(filesDir);
      }
      continue;
    }
    // Otherwise only notes. A library holds whatever the user puts in it, and
    // an image dropped next to a note is not a change to the library's
    // contents -- it is one filed *under* `files/` that is.
    if(path.extension() != ".md") continue;
    perf::addCounter(perf::CounterId::WatcherPathsApplied);
    listChanged = ui.state.refreshNoteFile(path) || listChanged;
    touchedAnything = true;
  }
  for(const auto& filesDir : filesDirs) ui.state.refreshFilesDir(filesDir);
  if(!touchedAnything) return !filesDirs.empty();
  // The catalog has already dropped its own memos for every file whose note-
  // list fields moved; what it cannot know about is the wiki-link cache the
  // shell keeps over the same list.
  if(listChanged) invalidateWikiNotes(ui);
  // Last, and only for the note on screen: the index is now right about every
  // file that moved, and this is the one whose bytes a person is looking at.
  reloadSelectedIfChangedOnDisk(ui);
  return true;
}

void beginRename(UiRuntime& ui) {
  if(ui.state.openNote().noteId().empty()) {
    ui.status = "Select a note before renaming";
    return;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "rename-note";
  overlay.title = "Rename note";
  overlay.value.beginWith(std::string(ui.state.selectedTitle()));
  overlay.placeholder = "Note title";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

void saveRename(UiRuntime& ui) {
  if(ui.fields.rename.empty()) {
    ui.status = "Rename needs a title";
    return;
  }
  invalidateWikiNotes(ui);
  const std::string asked = ui.fields.rename.text();
  if(ui.state.renameSelectedNote(asked, ui.editor.text())) {
    loadSelectedIntoEditor(ui);
    ui.focus = FocusArea::Editor;
    // A note is a file, and a folder cannot hold two files of one name, so a
    // title already taken beside it is given a numbered one instead. That is
    // the right answer -- a modal saying "no" would leave the reader to invent
    // a name they did not want either -- but it has to be *said*: renaming to a
    // name in use reported "Renamed note" and left a note called something the
    // reader never typed.
    const auto settled = ui.state.selectedTitle();
    ui.status = settled == asked ? "Renamed note"
                                 : "That name was taken -- renamed to " + std::string(settled);
  } else {
    ui.status = "Rename failed";
  }
}

bool createNote(UiRuntime& ui, const std::string& title) {
  if(!ui.state.catalog().isOpen()) {
    ui.status = "Start with --library <path> before creating notes";
    return false;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return false;
  const auto folder = ui.state.selection().folder;
  invalidateWikiNotes(ui);
  // An empty body, not a `# <title>` heading. The page draws the note's name
  // above its first block, so seeding one only put the name on screen twice and
  // left the caret on the second copy; the empty page prompts for a first line
  // instead, which is where the caret already is.
  // Before the create, which opens a tab and moves the selection: the note
  // being left is only findable while `loadedNoteId` still names it. Creating a
  // note is a move away from whatever was open, so coming back to it should
  // come back to where it was -- the same rule the other paths follow, and this
  // is the one that does not go through `loadSelectedBuffer` to get it.
  rememberPageScroll(ui);
  auto created = ui.state.createNote(title, folder, "");
  if(!created) {
    ui.status = "Could not create the note";
    return false;
  }
  ui.loadedNoteId = created->id;
  ui.editor.setText("");
  // A note with nothing in it is at its own top, whatever its tab may have
  // been told: it is new, so it has no place to have been left in.
  resetPageScroll(ui);
  ui.revealEditorCursor = true;
  ui.focus = FocusArea::Editor;
  ui.status = "Created " + created->title;
  return true;
}

void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  // The folder is chosen *before* the name is asked for, so the prompt's answer
  // needs nothing carried with it. Escaping the prompt leaves the sidebar
  // showing the notebook that was right-clicked, which is where the reader was
  // pointing when they asked.
  ui.state.selectFolder(folder);
  beginNoteCreate(ui);
}

bool saveCurrent(UiRuntime& ui, bool quiet) {
  if(!ui.state.catalog().isOpen()) {
    if(!quiet) ui.status = "No library open";
    return false;
  }
  if(ui.state.selection().noteId.empty()) {
    // No prompt on this path: a save the reader asked for, and an autosave they
    // did not, must not stop to open a modal over the note being written.
    createNote(ui, "Untitled");
  }
  const std::string wasNamed = ui.state.selection().noteId;
  const auto result = ui.state.saveSelectedNote(ui.editor.text());
  if(!result.ok) {
    ui.status = quiet ? "Autosave failed" : "Save failed";
    return false;
  }
  // The first save of a note that arrived without front matter gives it a
  // permanent id, and `AppState::followIdChange` re-points the selection, the
  // tabs, the favorites and the recents that named the old one. The buffer's
  // record of *which* note is in it is the one thing outside that reach, and
  // left stale it makes the shell believe the note on screen is a different
  // note from the one it loaded: the guard in `loadSelectedIntoEditor` stops
  // protecting the unsaved buffer, and the tab this note's scroll offset
  // belongs to can no longer be found.
  if(ui.loadedNoteId == wasNamed) ui.loadedNoteId = ui.state.selection().noteId;
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
