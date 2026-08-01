#pragma once

#include "CoreAliases.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "library/Organization.h"
#include "core/ui/ShellModel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micronotes::ui {

// The pane/shell model lives in src/core so microagenda shares it verbatim.
// Re-export it here so app code keeps writing ui::PaneMode rather than caring
// which side of the core boundary the type came from.
using microcore::ui::PaneMode;
using microcore::ui::ShellModel;

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
  const ShellModel& shell() const;
  ShellModel& shell();
  const UiSelection& selection() const;

  void selectFolder(std::filesystem::path folder);
  void selectTag(std::string tag);
  void selectNote(std::string noteId);
  void setSearch(std::string query, library::SearchScope scope = library::SearchScope::All);
  std::vector<library::FolderNode> folders() const;
  std::vector<std::string> tags() const;
  std::vector<library::NoteListItem> currentNotes() const;
  std::vector<library::NoteListItem> allNotes() const;
  std::vector<library::SearchResult> currentSearchResults() const;
  std::optional<LoadedNote> selectedNote() const;
  std::optional<library::NoteListItem> findNote(std::string_view noteId) const;
  std::optional<library::NoteListItem> createNote(const std::string& title, const std::filesystem::path& folder, std::string_view body = "");
  bool saveSelectedNote(std::string_view body);
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
  ShellModel shell_;
  UiSelection selection_;
  std::optional<library::Library> library_;
  mutable std::optional<library::OrganizationService> organization_;
  library::LibraryIndex index_;
};

}
