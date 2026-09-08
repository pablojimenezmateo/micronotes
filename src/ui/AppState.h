#pragma once

#include "CoreAliases.h"

#include "core/platform/DurableFile.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "library/Organization.h"
#include "library/RecoveryStore.h"
#include "ui/WorkspaceModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

struct UiSelection {
  std::filesystem::path folder;
  std::string tag;
  std::string noteId;
  std::string search;
  library::SearchScope searchScope = library::SearchScope::All;
};

struct LoadedNote {
  library::NoteListItem item;
  library::NoteMetadata metadata;
  std::string body;
};

// The note the editor has open, as it was last read from or written to disk.
//
// Every question about the open note that is not about its *body* is answered
// from here instead of by opening the file again. It used to be a full read and
// a front-matter parse per asker per library revision, and the library revision
// moved on every save: the page header, the right-hand panel and the save
// itself each re-read the whole note once a second while somebody was typing
// into it.
//
// `disk` is what makes the save path safe. It is the identity of the file the
// front matter and the body were read from, so a stat before writing says
// whether anything else has touched the note in the meantime -- which is the
// difference between overwriting a `git checkout` and noticing it.
struct OpenNote {
  std::string noteId;   // empty when nothing is open
  std::filesystem::path path;
  library::NoteMetadata metadata;
  platform::FileSignature disk;
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

// Why the editor's copy of a note is out of step with the file.
enum class DiskState {
  Agrees,     // the file is as micronotes last read or wrote it
  Changed,    // something else has rewritten it
  Vanished,   // it was there and is not any more
  Unknown     // the stat failed; treated as Changed everywhere it matters
};

class AppState {
public:
  bool openOrCreateLibrary(const std::filesystem::path& root);
  bool hasLibrary() const;
  const std::filesystem::path& libraryRoot() const;
  const WorkspaceModel& workspace() const;
  WorkspaceModel& workspace();
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
  // References into the organization service's memos. The sidebar reads all
  // three every frame; handing back copies meant rebuilding the note list, the
  // folder list and the tag list once per frame for a library that had not
  // changed since the last one.
  const std::vector<library::FolderNode>& folders() const;
  const std::vector<std::string>& tags() const;
  std::vector<library::NoteListItem> currentNotes() const;
  const std::vector<library::NoteListItem>& allNotes() const;
  std::vector<library::SearchResult> currentSearchResults() const;
  // Notes whose text links to the open one. Empty when nothing is open.
  std::vector<library::Backlink> backlinksToSelected() const;
  // The open note's front matter and the identity of its file. Opens the file
  // at most once per note rather than once per caller; see `OpenNote`.
  const OpenNote& openNote() const;
  // The open note's *body*, off the disk, with its front matter. The one reader
  // of a note's text: anything that wants a title, a tag or an icon wants
  // `openNote()` and costs nothing.
  std::optional<LoadedNote> readSelectedNote() const;
  // Whether the file still says what micronotes last read or wrote. One stat.
  DiskState selectedNoteDiskState() const;
  // The open note's name, as the library lists it. The note list already
  // applies the rule -- the front matter's `title`, or the file's stem when it
  // carries none -- so the four callers that re-derived it from the metadata
  // were recomputing an answer that was already sitting in the list. Empty when
  // nothing is open.
  std::string_view selectedTitle() const;
  std::optional<library::NoteListItem> findNote(std::string_view noteId) const;
  // The same lookup as a borrow. A caller that only reads what it found should
  // take this: `findNote` copies a path and three strings out of a list the
  // caller already holds a reference to.
  const library::NoteListItem* noteById(std::string_view noteId) const;
  // A note created here opens in a tab of its own like any other, which is what
  // hardcoding the old `false` got wrong: following a dead `[[wikilink]]`
  // created the note and then replaced the note that linked to it, losing the
  // context the link was followed from.
  std::optional<library::NoteListItem> createNote(const std::string& title,
                                                  const std::filesystem::path& folder,
                                                  std::string_view body = "",
                                                  ui::TabPolicy policy = ui::TabPolicy::NewTab);
  // Writes `body` to the open note.
  //
  // Refuses to destroy an external change: when the file no longer matches what
  // was read, the version on disk is filed beside the note as a note of its own
  // first, and `SaveResult::conflictCopy` names it. Nothing is silently
  // overwritten, and nothing stalls -- the buffer is written either way, so
  // typing does not stop because a sync daemon touched the file.
  //
  // The index update is for the one file that changed, not a rescan of the
  // library, and the note list's memos survive a save that only moved the body.
  SaveResult saveSelectedNote(std::string_view body);
  // Replaces the open note's front matter without touching its body. The three
  // header edits -- rename, icon, tags -- all take this route, and all of them
  // require the buffer to have been saved first: the body written back is the
  // one on disk.
  bool saveSelectedNoteHeader(const library::NoteMetadata& metadata);
  // Queues the recovery copy of the open note. Returns false when a write
  // posted earlier has since failed; see `library::RecoveryStore` for why the
  // answer is about an earlier post rather than this one.
  bool saveSelectedNoteRecovery(std::string_view body) const;
  bool clearSelectedNoteRecovery() const;
  std::optional<std::string> selectedRecoveryBody() const;
  bool renameSelectedNote(const std::string& title);
  // An empty icon removes the front matter key rather than writing it blank.
  bool setSelectedNoteIcon(const std::string& icon);
  // Appends text to a note that is not open, for moving blocks between notes.
  bool appendToNote(std::string_view noteId, std::string_view text);
  bool deleteSelectedNote();
  bool moveSelectedNoteToFolder(const std::filesystem::path& folder);
  bool createFolder(const std::filesystem::path& folder);
  bool renameSelectedFolder(const std::filesystem::path& folder);
  // Re-parents a folder. Refuses the moves that would lose it: the library
  // root, a move into itself, and a move into its own descendant.
  bool moveFolderInto(const std::filesystem::path& folder, const std::filesystem::path& newParent);
  bool deleteSelectedFolder();
  bool updateSelectedTags(const std::vector<std::string>& tags);
  // Re-reads the library from disk: a recursive walk, a stat per note, a read of
  // every row in the index, and then every memo the note list has built is
  // dropped. Right when *something* changed and nobody can say what -- a window
  // regaining focus, a folder operation -- and wrong on the save path, which
  // knows exactly which file it wrote.
  bool refreshLibrary();
  // Re-indexes one file, without a walk. Returns whether that changed anything
  // the note list shows. Used by the save path and by the watcher, both of
  // which are told which file moved.
  bool refreshNoteFile(const std::filesystem::path& path);
  // The same, for the file this app has just written: the front matter and body
  // are handed over instead of being read back out of it.
  bool refreshWrittenNoteFile(const std::filesystem::path& path,
                              const library::NoteMetadata& metadata, std::string_view body);
  // Drops the memos derived from the note list -- the list itself, the folder
  // counts, the tag list, the id map. For a caller that has re-indexed several
  // files with `refreshNoteFile` and wants to pay for one rebuild rather than
  // one per file.
  void invalidateNoteList();
  // Forgets everything read about the open note and re-indexes its file, so the
  // next question about it goes back to the disk. For a note that changed
  // underneath the app: the record is a memo of a file that no longer says
  // that. False when nothing is open.
  //
  // Re-points the selection when the file's front-matter `id` is what changed:
  // the note is still the note on screen, and letting it vanish because
  // somebody edited a line of YAML would be the wrong kind of correct.
  bool reloadSelectedNote();
  // Bumped every time the library is re-read. A view that caches an answer
  // derived from the library keys its cache on this rather than trying to name
  // every mutation that could have invalidated it.
  std::uint64_t revision() const;

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

  bool favorite(std::string_view noteId) const;
  bool toggleFavorite(const std::string& noteId);
  // Records a note as just opened. Newest first, and capped, so the list stays
  // a shortcut rather than a second library.
  void noteOpened(const std::string& noteId);

  std::vector<library::TrashEntry> trashEntries() const;
  bool restoreFromTrash(const std::string& name);

private:
  // Establishes `openNote_` for whatever the selection names, reading the file
  // when the selection has moved to a note this has not seen. `body` takes the
  // text when the caller wants it, so opening a note is one read rather than
  // one for the body and another for the front matter.
  const OpenNote& loadOpenNote(std::string* body) const;
  // Records a single-file re-index: moves the revision when a row actually
  // moved, and reports whether the note list has to be rebuilt.
  bool applied(const library::LibraryIndex::FileRefresh& refresh);

  WorkspaceModel workspace_;
  UiSelection selection_;
  std::optional<library::Library> library_;
  // The open note's front matter, read once per note. Mutable because reading
  // it is memoisation rather than a change of state: every asker holds a const
  // `AppState`.
  mutable std::optional<OpenNote> openNote_;
  mutable std::optional<library::OrganizationService> organization_;
  library::LibraryIndex index_;
  // Mutable because posting recovery is a side effect of editing, not of
  // changing the state: every caller of it holds a const `AppState`.
  mutable library::RecoveryStore recovery_;
  std::uint64_t revision_ = 0;
};

}
