#pragma once

#include "core/platform/PathUtils.h"
#include "CoreAliases.h"

#include "library/Metadata.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::library {

struct LoadedNote {
  NoteMetadata metadata;
  std::string body;
};

// The companion-files convention: a directory named `files` directly inside a
// notebook -- the library root included -- holds the PDFs, images and videos
// that belong with that notebook's notes. Everything under it, recursively, is
// a *file* rather than a note: listed in the tree, found by name, opened by the
// desktop's default handler, and never read, rendered or indexed by micronotes.
// A `.md` inside one is a file too, and a nested `files/` inside one is an
// ordinary folder.
//
// The name is spelled here and nowhere else. Every layer that has to know
// whether a path is a companion asks one of these three rather than comparing
// a component to a string, which is what keeps the rule one rule.
//
// The match is exact and case-sensitive: `Files` is a notebook.
inline constexpr std::string_view kFilesDirName = "files";

// Whether `relative` names a files directory itself: `<notebook>/files`.
bool isFilesDir(const std::filesystem::path& relative);
// Whether `relative` is a files directory or sits anywhere under one.
bool insideFilesDir(const std::filesystem::path& relative);
// The `<notebook>/files` prefix of `relative`, or empty when it has none.
std::filesystem::path filesRootOf(const std::filesystem::path& relative);

// One entry under a files directory, as the walk reports it. The `files`
// directory itself is the first entry of its subtree, with `directory` set.
struct CompanionEntry {
  std::filesystem::path path;    // library-relative
  std::filesystem::path folder;  // its parent, library-relative -- carried, like NoteListItem::folder
  bool directory = false;
};

// A note or folder waiting in `.micronotes/trash/`. Deletion is a move inside
// the library rather than a call to the system trash, because restoring has to
// work from inside micronotes and a system trash cannot be read back portably.
struct TrashEntry {
  std::string name;                      // file name inside `trash/files`
  std::filesystem::path originalRelative; // where it came from, relative to the root
  std::string title;
  std::string deletedAt;                 // local ISO-8601, for the restore list
  // The note's attachment directory, moved and restored with it.
  std::string attachmentName;
  std::filesystem::path attachmentOriginalRelative;
};

class Library {
public:
  explicit Library(std::filesystem::path root);
  const std::filesystem::path& root() const;
  void ensureLayout() const;
  std::filesystem::path notePath(const std::string& title) const;
  std::filesystem::path createNote(const NoteMetadata& metadata, std::string_view body) const;
  // The note's front matter and the body under it, with a leading `# <name>`
  // heading counted as part of the header rather than as body text. Every read
  // of a note goes through here, so nothing downstream has to know that a note
  // may or may not carry its own name twice.
  LoadedNote loadNote(const std::filesystem::path& path) const;
  NoteMetadata loadNoteMetadata(const std::filesystem::path& path) const;
  bool saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const;
  // Files whatever is at `path` right now as a note of its own, beside it, and
  // returns that note's file name. Empty when there was nothing there to keep
  // or the copy could not be written.
  //
  // For the moment a save discovers that something else -- another editor, a
  // sync daemon, a `git checkout` -- has rewritten a note under the buffer.
  // Both versions matter and neither may be dropped, so the one that is *not*
  // on screen becomes a note in the library: visible in the sidebar, indexed,
  // searchable, and openable side by side with the one that is. Hiding it in a
  // state directory would technically not lose it and practically would.
  //
  // The copy is given a fresh id rather than inheriting the original's. Two
  // notes carrying one id are one row in the index -- the upsert keys on it --
  // so an inherited id would make the copy and the note fight over which of
  // them the library lists.
  std::string preserveExternalVersion(const std::filesystem::path& path) const;
  std::filesystem::path createFolder(const std::filesystem::path& relativeFolder) const;
  // Writes the note under the file name `metadata.title` implies, and removes
  // the file it came from when that name changed. Returns where it landed, or
  // an empty path when the write failed.
  //
  // One durable write. `renameSelectedNote` used to go through `renameNote`,
  // which read the note, patched its title and wrote it -- and then wrote the
  // whole note *again* with the metadata it actually wanted. Two atomic writes,
  // four fsync barriers and a redundant read of the whole file, for one rename,
  // with a real ambiguity about which of the two writes won.
  std::filesystem::path saveNoteAs(const std::filesystem::path& path,
                                   const NoteMetadata& metadata, std::string_view body) const;
  // The same, for a caller that is not already holding the note: reads it, puts
  // `newTitle` in its front matter, and writes it under the matching name.
  std::filesystem::path renameNote(const std::filesystem::path& path, const std::string& newTitle) const;
  std::filesystem::path moveNote(const std::filesystem::path& path, const std::filesystem::path& relativeFolder) const;
  std::filesystem::path renameFolder(const std::filesystem::path& relativeFolder, const std::filesystem::path& newRelativeFolder) const;
  void deleteFolder(const std::filesystem::path& relativeFolder) const;
  void deleteNote(const std::filesystem::path& path) const;
  // Newest first, so the restore list reads as an undo history.
  std::vector<TrashEntry> trashEntries() const;
  // Puts an entry back where it came from, renaming around anything that has
  // taken its place. False when the trashed file is no longer there.
  bool restoreFromTrash(const std::string& name) const;
  // Every note in the library, by path. For a caller that has no use for the
  // stat each entry carries; everything on a measured path takes `walk`.
  std::vector<std::filesystem::path> noteFiles() const;

  // The one walk of the tree, with the directories it passed through reported
  // alongside the notes. Either output may be null.
  //
  // A `directory_entry` caches the stat it performs, so a caller that needs
  // both a file's size and its modification time pays one syscall instead of
  // the two the free functions make -- which is the whole cost of a refresh
  // that finds nothing changed.
  //
  // The directories are here because the sidebar tree needs them -- including
  // the empty ones, which no list of notes can name -- and the alternative was
  // a second `recursive_directory_iterator` over the same tree a few
  // microseconds later. Paths are library-relative, and the state directory is
  // pruned rather than filtered, so neither list can mention it.
  //
  // `companions` receives everything under a files directory -- see
  // `kFilesDirName` -- and nothing under one reaches the other two outputs. A
  // note inside a files directory would otherwise be a note the index knows
  // and the tree cannot place.
  void walk(std::vector<std::filesystem::directory_entry>* files,
            std::vector<std::filesystem::path>* directories,
            std::vector<CompanionEntry>* companions = nullptr) const;

  // The companion entries under one `<notebook>/files` directory, without a
  // walk of anything else. For the watcher: a PDF copied into a folder must not
  // cost the whole-library refresh a folder operation does. Empty when the
  // directory is gone, which is the answer for a files directory just deleted.
  std::vector<CompanionEntry> walkFilesDir(const std::filesystem::path& relativeFilesDir) const;

  // Moves or renames a companion entry -- a file or a folder inside a files
  // directory -- to `newRelative`, creating the target's parent and numbering
  // around anything already there. Returns where it landed, or empty when the
  // move was refused or failed. Refused: the `files` directory itself, which is
  // the anchor of the convention and would stop being one under another name.
  std::filesystem::path moveCompanion(const std::filesystem::path& relative,
                                      const std::filesystem::path& newRelative) const;
  // Moves a companion entry into the trash, restorable through
  // `restoreFromTrash` like a note: the same index line, minus the attachment
  // half a note carries and a file does not.
  bool deleteCompanion(const std::filesystem::path& relative) const;

private:
  // Every row of the trash index. The two readers of it differ in what they
  // keep, not in how they read; see the definition.
  std::vector<TrashEntry> readTrashIndex() const;

  std::filesystem::path root_;
  // The same root, canonicalized once. Every path this class is handed is
  // checked against it, and resolving the root's half per call was the largest
  // single cost in reading a note -- see `platform::SafeRoot`.
  platform::SafeRoot safeRoot_;
};

}
