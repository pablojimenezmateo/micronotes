#pragma once

#include "CoreAliases.h"

#include "library/Library.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
  std::string icon;   // the note's `icon:` front matter, empty when it has none
  // The library-relative directory the note sits in, empty at the root. Derived
  // from `path` and carried rather than recomputed: `lexically_relative` plus
  // `parent_path` is two path allocations, and it was being done per note per
  // caller by the folder filter, the tree, the sidebar and the breadcrumb --
  // all of them asking the same question about the same unchanged list.
  std::filesystem::path folder;
};

class OrganizationService {
public:
  explicit OrganizationService(const Library& library);

  // Every note in the library, sorted by title. The sidebar tree needs all of
  // them at once, not one folder at a time.
  const std::vector<NoteListItem>& notes() const;
  // References, not copies. All three are memoised, and the sidebar asks for
  // them on every frame: returning by value deep-copied the whole library --
  // a path and three strings per note -- to answer a question already answered.
  const std::vector<FolderNode>& folders() const;
  const std::vector<std::string>& tags() const;
  std::vector<NoteListItem> notesInFolder(const std::filesystem::path& relativeFolder) const;
  std::vector<NoteListItem> notesWithTag(const std::string& tag) const;
  // The note with this id, or nothing. A hash lookup: this is asked once per
  // search result and once per shortcut and once per selection change, and as a
  // linear scan of a thousand notes with a string compare each it made
  // "show me the results" quadratic in the size of the library.
  std::optional<NoteListItem> findNote(std::string_view noteId) const;
  // The same lookup without the copy, for a caller that only wants to read the
  // note it found. `findNote` deep-copies a path and three strings.
  const NoteListItem* noteById(std::string_view noteId) const;

private:
  const Library& library_;
  mutable std::optional<std::vector<NoteListItem>> notes_;
  // Keys are views into the ids in `notes_`, so this is filled by the same call
  // that finalises that vector and never outlives it.
  mutable std::unordered_map<std::string_view, std::size_t> index_;
  // Every directory under the root, library-relative, from the same walk that
  // built `notes_`. The tree needs the empty ones, which the note list cannot
  // name; before this they cost a second walk of the whole library.
  mutable std::vector<std::filesystem::path> directories_;
  mutable std::optional<std::vector<FolderNode>> folders_;
  mutable std::optional<std::vector<std::string>> tags_;
};

}
