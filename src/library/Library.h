#pragma once

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
  LoadedNote loadNote(const std::filesystem::path& path) const;
  std::string loadNoteBody(const std::filesystem::path& path) const;
  NoteMetadata loadNoteMetadata(const std::filesystem::path& path) const;
  bool saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const;
  bool updateTags(const std::filesystem::path& path, const std::vector<std::string>& tags) const;
  std::filesystem::path createFolder(const std::filesystem::path& relativeFolder) const;
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
  std::vector<std::filesystem::path> noteFiles() const;

  // The same walk, but keeping each directory_entry rather than just its path.
  // A directory_entry caches the stat it performs, so a caller that needs both
  // the size and the modification time pays one syscall instead of two -- which
  // is the whole cost of a refresh that finds nothing changed.
  std::vector<std::filesystem::directory_entry> noteFileEntries() const;

private:
  std::filesystem::path root_;
};

}
