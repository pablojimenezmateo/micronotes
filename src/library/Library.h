#pragma once

#include "library/Metadata.h"

#include <filesystem>
#include <optional>
#include <string>

namespace micronotes::library {

struct LoadedNote {
  NoteMetadata metadata;
  std::string body;
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
  void saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const;
  void updateTags(const std::filesystem::path& path, const std::vector<std::string>& tags) const;
  std::filesystem::path createFolder(const std::filesystem::path& relativeFolder) const;
  std::filesystem::path renameNote(const std::filesystem::path& path, const std::string& newTitle) const;
  std::filesystem::path moveNote(const std::filesystem::path& path, const std::filesystem::path& relativeFolder) const;
  std::filesystem::path renameFolder(const std::filesystem::path& relativeFolder, const std::filesystem::path& newRelativeFolder) const;
  void deleteFolder(const std::filesystem::path& relativeFolder) const;
  void deleteNote(const std::filesystem::path& path) const;
  std::vector<std::filesystem::path> noteFiles() const;

private:
  std::filesystem::path root_;
};

}
