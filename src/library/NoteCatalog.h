#pragma once

#include "CoreAliases.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "library/Organization.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::library {

// The library, its index and the memos over both, kept in step.
//
// The three used to be three fields of `ui::AppState` beside the selection, the
// window arrangement and the open note, and keeping them in step was written
// out at each site that changed a file: write, then re-index the file that was
// written, then decide whether the note list's memos had to be dropped. That is
// one rule with three halves, so it is one class -- every write below performs
// its own re-index and drops exactly the memos the re-index invalidated, and a
// caller that forgets to is no longer a thing that can be written.
//
// It answers for a library that is not open. `isOpen()` is the question, and
// every read has an empty answer rather than a precondition, because the shell
// draws a window before a library is chosen and every surface in it asks.
class NoteCatalog {
public:
  bool open(const std::filesystem::path& root);
  bool isOpen() const;
  const std::filesystem::path& root() const;
  // Bumped every time the library is re-read, and every time a row in the index
  // actually moves. A view that caches an answer derived from the library keys
  // its cache on this rather than trying to name every mutation that could have
  // invalidated it.
  std::uint64_t revision() const;

  // --- reading -------------------------------------------------------------
  //
  // References into the memos where a reference will do. The sidebar reads the
  // note list, the folder list and the tag list every frame; handing back
  // copies rebuilt all three once per frame for a library that had not changed.

  const std::vector<NoteListItem>& notes() const;
  const std::vector<FolderNode>& folders() const;
  const std::vector<std::string>& tags() const;
  std::vector<NoteListItem> notesInFolder(const std::filesystem::path& folder) const;
  std::vector<NoteListItem> notesWithTag(const std::string& tag) const;
  std::optional<NoteListItem> findNote(std::string_view noteId) const;
  // The same lookup as a borrow. A caller that only reads what it found should
  // take this: `findNote` copies a path and three strings out of a list the
  // caller already holds a reference to.
  const NoteListItem* noteById(std::string_view noteId) const;
  std::vector<SearchResult> search(std::string_view query, SearchScope scope) const;
  // Every companion entry -- see `kFilesDirName` -- in path order.
  const std::vector<CompanionEntry>& companions() const;
  // The companions whose *name* matches the query. Never their contents, which
  // is why `SearchScope::Content` finds none: a companion is not read.
  std::vector<CompanionEntry> searchCompanions(std::string_view query, SearchScope scope) const;
  // Notes whose text links to one named `title` or filed under `stem`.
  std::vector<Backlink> backlinks(std::string_view title, std::string_view stem) const;
  std::vector<TrashEntry> trashEntries() const;
  // A note's front matter and body, off the disk. For a note that is not the
  // open one; the open note's are memoised by `ui::OpenNoteRecord`.
  LoadedNote loadNote(const std::filesystem::path& path) const;
  // A title nothing else in `folder` has taken, `requested` if that is free.
  // `keep` is a path allowed to hold the name already -- the note being renamed.
  std::string uniqueTitle(const std::string& requested,
                          const std::filesystem::path& folder = {},
                          const std::filesystem::path& keep = {}) const;

  // --- writing -------------------------------------------------------------
  //
  // Each of these writes a file and leaves the index and the memos describing
  // what it wrote. The ones that touch a single note re-index that one file;
  // the ones that can create or remove a *directory* re-read the library,
  // because the sidebar tree is drawn from the directories the walk reports and
  // no list of notes can name an empty one.

  // Writes a note in place. `body` and `metadata` are what was written, so the
  // re-index costs a stat and a transaction rather than a read of the file.
  bool writeNote(const std::filesystem::path& path, const NoteMetadata& metadata,
                 std::string_view body);
  // Writes a note under the file name its title implies, removing the file it
  // came from when that name changed. Returns where it landed, empty on failure.
  std::filesystem::path writeNoteAs(const std::filesystem::path& path,
                                    const NoteMetadata& metadata, std::string_view body);
  // Creates a note, optionally in a folder, and returns it as the list holds
  // it. The permanent id and the collision-free title are this class's to
  // decide: both are questions about the library, and every caller that had to
  // answer them itself was one that could get them wrong.
  std::optional<NoteListItem> createNote(const std::string& title,
                                         const std::filesystem::path& folder,
                                         std::string_view body);
  // Files whatever is at `path` right now as a note of its own beside it, and
  // returns that note's file name. See `Library::preserveExternalVersion`.
  std::string preserveExternalVersion(const std::filesystem::path& path);
  void deleteNote(const std::filesystem::path& path);
  std::filesystem::path moveNote(const std::filesystem::path& path,
                                 const std::filesystem::path& folder);
  std::filesystem::path createFolder(const std::filesystem::path& folder);
  std::filesystem::path renameFolder(const std::filesystem::path& folder,
                                     const std::filesystem::path& newFolder);
  void deleteFolder(const std::filesystem::path& folder);
  bool restoreFromTrash(const std::string& name);

  // Companion files. Each of these moves something inside a files directory
  // and then re-walks only the files directories it touched -- a rename is a
  // rename, not a reason to stat every note in the library.
  //
  // `renameCompanion` keeps the entry where it is and changes its last
  // component; `moveCompanion` keeps its name and changes the directory it sits
  // in, which may be another notebook's `files/`, created on demand. Both
  // return where the entry landed, empty when refused: the `files` directory
  // itself, a move to the folder it is already in, or a directory into itself.
  std::filesystem::path renameCompanion(const std::filesystem::path& relative,
                                        const std::string& newName);
  std::filesystem::path moveCompanion(const std::filesystem::path& relative,
                                      const std::filesystem::path& destinationDir);
  bool deleteCompanion(const std::filesystem::path& relative);
  // A folder inside a files directory. `relative` names the folder to create,
  // and it has to sit under a `files/`: a folder anywhere else is a notebook.
  std::filesystem::path createCompanionFolder(const std::filesystem::path& relative);

  // --- keeping up with what changed underneath -----------------------------

  // Re-reads the library from disk: a recursive walk, a stat per note, a read
  // of every row in the index, and then every memo is dropped. Right when
  // *something* changed and nobody can say what -- a window regaining focus, a
  // folder operation -- and wrong on the save path, which knows what it wrote.
  bool refresh();
  // Re-indexes one file, without a walk, and reports whether that changed any
  // of the five fields the note list is built from. The memos are already
  // dropped when it did; the answer is for a caller holding caches of its own.
  bool refreshFile(const std::filesystem::path& path);
  // Re-walks one `<notebook>/files` directory and swaps its entries into the
  // list. For a change the watcher reported under one: the narrow answer to the
  // question `refresh()` answers by re-reading everything. Bumps the revision,
  // so every view keyed on it sees the new list.
  void refreshFilesDir(const std::filesystem::path& filesDir);

private:
  // Records a single-file re-index: moves the revision when a row actually
  // moved, drops the memos when a note-list field did, and reports the latter.
  bool applied(const LibraryIndex::FileRefresh& refresh);
  // Re-emplaced rather than asked to forget: the memos are the whole of its
  // state, so a fresh one is the cheapest possible invalidation and there is no
  // second code path holding a half-cleared service.
  void invalidateNoteList();

  std::optional<Library> library_;
  LibraryIndex index_;
  mutable std::optional<OrganizationService> organization_;
  std::uint64_t revision_ = 0;
};

}
