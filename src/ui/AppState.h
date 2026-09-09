#pragma once

#include "CoreAliases.h"

#include "library/NoteCatalog.h"
#include "library/Organization.h"
#include "library/SearchScope.h"
#include "ui/OpenNoteRecord.h"
#include "ui/WorkspaceModel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// What the sidebar's list is showing, and which note is open.
//
// The three filters are mutually exclusive -- a list is a folder's notes, or a
// tag's, or a search's, never two -- and that rule is the whole reason this is
// a type with methods rather than five fields. It used to be arithmetic
// repeated at each site that moved the selection: `folder = x; tag.clear();
// search.clear();`, five times. Four of the five spelled it out and
// `renameSelectedFolder` did not, so renaming a folder while a tag was
// selected left the sidebar filtered by a tag and titled after a folder, with
// nothing to say which of the two the list was.
struct UiSelection {
  std::filesystem::path folder;
  std::string tag;
  std::string noteId;
  std::string search;
  library::SearchScope searchScope = library::SearchScope::All;

  // Show a folder's notes. Clears the other two filters.
  void showFolder(std::filesystem::path chosen) {
    folder = std::move(chosen);
    tag.clear();
    clearSearch();
  }
  // Show a tag's notes. Clears the other two filters.
  void showTag(std::string chosen) {
    tag = std::move(chosen);
    folder.clear();
    clearSearch();
  }
  // Show what a query matches. The scope travels with the query because an
  // empty query means "not searching" and a scope without one means nothing.
  void showSearch(std::string query, library::SearchScope scope) {
    search = std::move(query);
    searchScope = scope;
  }
  void clearSearch() {
    search.clear();
    searchScope = library::SearchScope::All;
  }
};

// What a save did.
//
// A bool could not say the one thing worth saying: that the note had been
// rewritten underneath the buffer, and that the version which was there has
// been kept as a note of its own rather than destroyed.
struct SaveResult {
  bool ok = false;
  // The file name the externally-changed version was filed under, beside the
  // note. Empty on an ordinary save.
  std::string conflictCopy;
};

// What is selected, how the window is arranged, and the commands that change a
// note because of it.
//
// It used to be five things behind one door: the library and its index, the
// selection, the workspace, the open note's record, and every write that
// reaches a file. Two of the five are their own types now --
// `library::NoteCatalog` and `ui::OpenNoteRecord` -- and what is left is the
// part that genuinely needs more than one of them: a command like "delete the
// selected note" is a library write, a memo to forget and a tab to close, and
// the class that owns the join is the one place those three stay in step.
//
// Everything that is only about the library is asked of `catalog()`; everything
// that is only about the open note's file is asked of `openNote()`.
class AppState {
public:
  bool openOrCreateLibrary(const std::filesystem::path& root);

  // The library, its index and the memos over both. Const: a surface reads the
  // library, and every write to it is a command below, because a write is never
  // only a write -- the selection, the open note's record and the tab strip all
  // follow one.
  const library::NoteCatalog& catalog() const;
  // The open note as last read from or written to disk, and the draft of it
  // that survives a crash. Const for the same reason.
  const OpenNoteRecord& openNote() const;

  // --- what is selected, and what the window is showing --------------------

  // The arrangement of the window, to read. Const because it is read at ten
  // times the rate it is written and the mutable overload was what got picked:
  // `workspace()` handed out a writable reference to the whole view model at
  // every one of those sites, so `AppState`'s encapsulation was whatever
  // `WorkspaceModel` happened to make public.
  const WorkspaceModel& workspace() const;
  // The same thing, to change. Named rather than an overload so that a site
  // which writes says so, and a reader can find every site that does.
  WorkspaceModel& editWorkspace();
  const UiSelection& selection() const;

  void selectFolder(std::filesystem::path folder);
  void selectTag(std::string tag);
  // Opening a note is opening a tab on it, and the note being read stays open.
  // `TabPolicy::Reuse` takes over the tab showing instead, which only the
  // keyboard cursor walking the sidebar wants; see `ui::TabPolicy`.
  void selectNote(std::string noteId, ui::TabPolicy policy = ui::TabPolicy::NewTab);
  // Closes a tab and selects whatever is left showing.
  void closeTab(std::size_t index);
  // Moves to the next or previous tab, wrapping.
  void stepTab(int delta);
  void setSearch(std::string query, library::SearchScope scope = library::SearchScope::All);

  // --- the library, through the selection ----------------------------------

  // The notes the sidebar's list is showing: a folder's, a tag's, or a query's.
  std::vector<library::NoteListItem> currentNotes() const;
  std::vector<library::SearchResult> currentSearchResults() const;
  // Notes whose text links to the open one. Empty when nothing is open.
  std::vector<library::Backlink> backlinksToSelected() const;
  // The open note's name, as the library lists it. The note list already
  // applies the rule -- the front matter's `title`, or the file's stem when it
  // carries none -- so the four callers that re-derived it from the metadata
  // were recomputing an answer that was already sitting in the list. Empty when
  // nothing is open.
  std::string_view selectedTitle() const;

  // --- commands that reach the disk ----------------------------------------
  //
  // Everything below here changes a file in the reader's library. Grouped
  // because the difference matters and the header did not show it: the
  // note-writing path sat between `selectedTitle` and `toggleFavorite`, in one
  // flat list of fifty-five, and nothing said which of them could lose an
  // afternoon's work.
  //
  // Every one of them that rewrites the open note goes through one guarded
  // write. A note's file is never written without checking what is there first
  // -- `OpenNoteRecord` carries a `platform::FileSignature` per open note and
  // it is compared before overwriting -- because a path that skips that
  // comparison destroys an edit made in another program. If two versions exist
  // and cannot be merged, both end up in the library; micronotes does not
  // choose. See `docs/library-format.md`, "Changes Made Outside micronotes",
  // and the rule in `AGENTS.md`.

  // Writes `body` to the open note.
  //
  // Refuses to destroy an external change: when the file no longer matches what
  // was read, the version on disk is filed beside the note as a note of its own
  // first, and `SaveResult::conflictCopy` names it. Nothing is silently
  // overwritten, and nothing stalls -- the buffer is written either way, so
  // typing does not stop because a sync daemon touched the file.
  SaveResult saveSelectedNote(std::string_view body);

  // The three header edits. Each takes the body the *editor* is holding rather
  // than reading one off the disk, which is what makes them safe: the version
  // that reaches the file is the one on screen. They used to read the body
  // back, which was correct only while every route into them happened to save
  // first -- an invariant nothing stated and no test held.
  bool renameSelectedNote(const std::string& title, std::string_view body);
  // An empty icon removes the front matter key rather than writing it blank.
  bool setSelectedNoteIcon(const std::string& icon, std::string_view body);
  bool updateSelectedTags(const std::vector<std::string>& tags, std::string_view body);

  // Removes `tag` from every note carrying it, and reports how many notes
  // changed. `openBody` is what the editor is holding: the open note is one of
  // the notes being rewritten, and it is the one whose file must not be written
  // from a body read off the disk.
  std::size_t removeTagEverywhere(std::string_view tag, std::string_view openBody);

  // A note created here opens in a tab of its own like any other, which is what
  // hardcoding the old `false` got wrong: following a dead `[[wikilink]]`
  // created the note and then replaced the note that linked to it, losing the
  // context the link was followed from.
  std::optional<library::NoteListItem> createNote(const std::string& title,
                                                  const std::filesystem::path& folder,
                                                  std::string_view body = "",
                                                  ui::TabPolicy policy = ui::TabPolicy::NewTab);
  // Appends text to a note that is not open, for moving blocks between notes.
  bool appendToNote(std::string_view noteId, std::string_view text);
  bool deleteSelectedNote();
  bool moveSelectedNoteToFolder(const std::filesystem::path& folder);
  bool restoreFromTrash(const std::string& name);

  bool createFolder(const std::filesystem::path& folder);
  bool renameSelectedFolder(const std::filesystem::path& folder);
  // Re-parents a folder. Refuses the moves that would lose it: the library
  // root, a move into itself, and a move into its own descendant.
  bool moveFolderInto(const std::filesystem::path& folder, const std::filesystem::path& newParent);
  bool deleteSelectedFolder();

  // Companion files -- see `library::kFilesDirName`. None of these touch the
  // selection: a companion is never the open note or the current notebook, so
  // there is nothing here to re-point. The catalog does the moving and the
  // refusing; these exist so every disk command the shell runs still goes
  // through one door.
  std::filesystem::path renameCompanion(const std::filesystem::path& relative,
                                        const std::string& newName);
  std::filesystem::path moveCompanion(const std::filesystem::path& relative,
                                      const std::filesystem::path& destinationDir);
  bool deleteCompanion(const std::filesystem::path& relative);
  std::filesystem::path createCompanionFolder(const std::filesystem::path& relative);
  // A change the watcher reported under one `<notebook>/files` directory.
  void refreshFilesDir(const std::filesystem::path& filesDir);

  // The view-state file beside the library: which notes are in tabs, how wide
  // the panels are, which bands are shut.
  //
  // Two lines each, and they are here rather than gone because *restoring*
  // moves the selection, and the selection is this class's to move. The format
  // itself -- a hundred and sixty lines of it, with its own
  // keys-are-added-never-redefined compatibility story -- is
  // `ui/UiStateFile.h`, which is where it belongs: it had nothing to do with
  // deciding whether a note can safely be written to disk, and shared a
  // translation unit with that only because it had `workspace_` in scope.
  bool saveUiState(const std::filesystem::path& path) const;
  bool loadUiState(const std::filesystem::path& path);

  // --- keeping up with what changed underneath -----------------------------

  // Re-reads the whole library. Right when *something* changed and nobody can
  // say what -- a window regaining focus -- and wrong on the save path, which
  // knows exactly which file it wrote.
  bool refreshLibrary();
  // Re-indexes one file, without a walk. Returns whether that changed anything
  // the note list shows. Used by the watcher, which is told which file moved.
  bool refreshNoteFile(const std::filesystem::path& path);
  // Forgets everything read about the open note and re-indexes its file, so the
  // next question about it goes back to the disk. For a note that changed
  // underneath the app: the record is a memo of a file that no longer says
  // that. False when nothing is open.
  //
  // Re-points the selection when the file's front-matter `id` is what changed:
  // the note is still the note on screen, and letting it vanish because
  // somebody edited a line of YAML would be the wrong kind of correct.
  bool reloadSelectedNote();

private:
  // Whether the file the open note came from keeps its name, or takes the one
  // `metadata.title` implies. A rename is the same write as a save with the
  // same guard in front of it; the only difference is this.
  enum class NamePolicy { Keep, FollowTitle };

  // The one path that rewrites the open note's file. Everything above that
  // writes a note -- the save, the rename, the icon, the tags -- arrives here,
  // so the external-change check, the record update and the retired recovery
  // copy are written once rather than four times and forgotten in one of them.
  SaveResult writeOpenNote(library::NoteMetadata metadata, std::string_view body,
                           NamePolicy naming);
  // A note that arrived without front matter gets a permanent id the first time
  // micronotes writes it, so its identity survives a later move.
  void adoptIdIfMissing(library::NoteMetadata& metadata) const;
  // The note's id moved. Everything pointing at the old one -- the selection,
  // its tab, the favorites, the recents -- has to follow, or the note the user
  // is looking at disappears out from under them.
  void followIdChange(const std::string& previous, const std::string& adopted);

  library::NoteCatalog catalog_;
  UiSelection selection_;
  WorkspaceModel workspace_;
  // Bound to the two above at construction: the record is a memo *of* whatever
  // is selected, so it reads them rather than being pushed at. Declared last so
  // both are alive before it binds.
  OpenNoteRecord openNote_ {catalog_, selection_.noteId};
};

}
