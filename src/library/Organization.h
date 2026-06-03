#pragma once

#include "library/Library.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::library {

struct FolderNode {
  std::filesystem::path path;
  int noteCount = 0;
};

struct NoteListItem {
  std::string id;
  std::filesystem::path path;
  std::string title;
  std::vector<std::string> tags;
};

class OrganizationService {
public:
  explicit OrganizationService(const Library& library);

  std::vector<FolderNode> folders() const;
  std::vector<std::string> tags() const;
  std::vector<NoteListItem> notesInFolder(const std::filesystem::path& relativeFolder) const;
  std::vector<NoteListItem> notesWithTag(const std::string& tag) const;
  std::optional<NoteListItem> findNote(std::string_view noteId) const;

private:
  const std::vector<NoteListItem>& allNotes() const;

  const Library& library_;
  mutable std::optional<std::vector<NoteListItem>> notes_;
  mutable std::optional<std::vector<FolderNode>> folders_;
  mutable std::optional<std::vector<std::string>> tags_;
};

}
