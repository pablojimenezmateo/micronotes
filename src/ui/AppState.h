#pragma once

#include "CoreAliases.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "library/Organization.h"
#include "library/RecoveryStore.h"
#include "ui/WorkspaceModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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
  // Opening a note is opening a tab on it. `inNewTab` keeps whatever was open
  // instead of replacing it, which is what a middle click or Ctrl+click means.
  void selectNote(std::string noteId, bool inNewTab = false);
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
  std::optional<LoadedNote> selectedNote() const;
  std::optional<library::NoteListItem> findNote(std::string_view noteId) const;
  // The same lookup as a borrow. A caller that only reads what it found should
  // take this: `findNote` copies a path and three strings out of a list the
  // caller already holds a reference to.
  const library::NoteListItem* noteById(std::string_view noteId) const;
  std::optional<library::NoteListItem> createNote(const std::string& title, const std::filesystem::path& folder, std::string_view body = "");
  bool saveSelectedNote(std::string_view body);
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
  bool refreshLibrary();
  // Bumped every time the library is re-read. A view that caches an answer
  // derived from the library keys its cache on this rather than trying to name
  // every mutation that could have invalidated it.
  std::uint64_t revision() const;

  bool favorite(std::string_view noteId) const;
  bool toggleFavorite(const std::string& noteId);
  // Records a note as just opened. Newest first, and capped, so the list stays
  // a shortcut rather than a second library.
  void noteOpened(const std::string& noteId);

  std::vector<library::TrashEntry> trashEntries() const;
  bool restoreFromTrash(const std::string& name);

  bool saveUiState(const std::filesystem::path& path) const;
  bool loadUiState(const std::filesystem::path& path);

private:
  WorkspaceModel workspace_;
  UiSelection selection_;
  std::optional<library::Library> library_;
  mutable std::optional<library::OrganizationService> organization_;
  library::LibraryIndex index_;
  // Mutable because posting recovery is a side effect of editing, not of
  // changing the state: every caller of it holds a const `AppState`.
  mutable library::RecoveryStore recovery_;
  std::uint64_t revision_ = 0;
};

}
