#pragma once

#include "CoreAliases.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"

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
  // The index is where the note list comes from: it has already read every
  // note's front matter, so the list is a `SELECT` rather than a second walk of
  // the library and a re-open of every file in it. The library is still here
  // for the root path, and as the fallback for an index that would not open --
  // a library whose SQLite file cannot be written must still list its notes.
  OrganizationService(const Library& library, const LibraryIndex& index);

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

  // Every companion entry -- see `Library::kFilesDirName` -- sorted by path.
  // Memoised like the folders, and off the same walk: the refresh reports them
  // beside the directories, so listing them costs no second pass of the tree.
  const std::vector<CompanionEntry>& companions() const;
  // Swaps the entries under one `<notebook>/files` directory for `entries`,
  // leaving every other subtree as it was. What the watcher applies when a
  // change lands under a files directory, so a PDF copied in is one small walk
  // and a memo patch rather than a re-read of the library.
  void replaceCompanionsUnder(const std::filesystem::path& filesDir,
                              std::vector<CompanionEntry> entries);
  // The companions whose file name contains `query`, case-insensitively for
  // ASCII, directories excluded. Capped like the index search is. Name only,
  // and deliberately: a companion's contents are not micronotes' to read.
  std::vector<CompanionEntry> companionsMatching(std::string_view query) const;

private:
  // Stores the list in path order, which both readers rely on.
  void setCompanions(std::vector<CompanionEntry> entries) const;

  const Library& library_;
  const LibraryIndex& index_;
  mutable std::optional<std::vector<NoteListItem>> notes_;
  // Keys are views into the ids in `notes_`, so this is filled by the same call
  // that finalises that vector and never outlives it.
  mutable std::unordered_map<std::string_view, std::size_t> byId_;
  // Every directory under the root, library-relative. Taken from the index's
  // own refresh walk when the index is open, and from a walk of our own only on
  // the fallback path. The tree needs the empty ones, which no list of notes
  // can name.
  mutable std::vector<std::filesystem::path> directories_;
  mutable std::optional<std::vector<CompanionEntry>> companions_;
  mutable std::optional<std::vector<FolderNode>> folders_;
  mutable std::optional<std::vector<std::string>> tags_;
};

}
