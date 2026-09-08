#pragma once

#include "core/platform/PathUtils.h"
#include "CoreAliases.h"

#include "library/Metadata.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micronotes::library {

struct LoadedNote {
  NoteMetadata metadata;
  std::string body;
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
  void walk(std::vector<std::filesystem::directory_entry>* files,
            std::vector<std::filesystem::path>* directories) const;

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
