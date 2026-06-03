#include "ui/AppState.h"

#include "library/Metadata.h"
#include "perf/Perf.h"
#include "platform/PathUtils.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace micronotes::ui {
namespace {

static std::string uniqueTitle(const library::Library& library, const std::string& requested, const std::filesystem::path& folder = {}, const std::filesystem::path& currentPath = {}) {
  const auto base = requested.empty() ? "Untitled" : requested;
  std::string candidate = base;
  const auto targetDir = folder.empty() ? library.root() : library.root() / folder;
  int suffix = 2;
  while(true) {
    const auto candidatePath = targetDir / (platform::sanitizeFileStem(candidate) + ".md");
    if(!std::filesystem::exists(candidatePath) || (!currentPath.empty() && std::filesystem::equivalent(candidatePath, currentPath))) break;
    candidate = base + "-" + std::to_string(suffix++);
  }
  return candidate;
}

}

bool AppState::openOrCreateLibrary(const std::filesystem::path& root) {
  perf::ScopeTimer timer("app_state.open_or_create_library");
  library_.emplace(root);
  library_->ensureLayout();
  organization_.emplace(*library_);
  return index_.open(root) && index_.refreshChangedFiles();
}

bool AppState::hasLibrary() const {
  return library_.has_value();
}

const std::filesystem::path& AppState::libraryRoot() const {
  static const std::filesystem::path empty;
  return library_ ? library_->root() : empty;
}

const ShellModel& AppState::shell() const {
  return shell_;
}

ShellModel& AppState::shell() {
  return shell_;
}

const UiSelection& AppState::selection() const {
  return selection_;
}

void AppState::selectFolder(std::filesystem::path folder) {
  selection_.folder = std::move(folder);
  selection_.tag.clear();
  selection_.search.clear();
}

void AppState::selectTag(std::string tag) {
  selection_.tag = std::move(tag);
  selection_.folder.clear();
  selection_.search.clear();
}

void AppState::selectNote(std::string noteId) {
  selection_.noteId = std::move(noteId);
}

void AppState::setSearch(std::string query, library::SearchScope scope) {
  selection_.search = std::move(query);
  selection_.searchScope = scope;
}

std::vector<library::FolderNode> AppState::folders() const {
  if(!organization_) return {};
  return organization_->folders();
}

std::vector<std::string> AppState::tags() const {
  if(!organization_) return {};
  return organization_->tags();
}

std::vector<library::NoteListItem> AppState::currentNotes() const {
  if(!library_ || !organization_) return {};
  if(!selection_.search.empty()) {
    std::vector<library::NoteListItem> out;
    for(const auto& result : index_.search(selection_.search, selection_.searchScope)) {
      auto item = organization_->findNote(result.id);
      if(item) out.push_back(std::move(*item));
      else out.push_back({result.id, result.path, result.title, {}});
    }
    return out;
  }
  if(!selection_.tag.empty()) return organization_->notesWithTag(selection_.tag);
  return organization_->notesInFolder(selection_.folder);
}

std::vector<library::SearchResult> AppState::currentSearchResults() const {
  if(!library_ || selection_.search.empty()) return {};
  return index_.search(selection_.search, selection_.searchScope);
}

std::optional<LoadedNote> AppState::selectedNote() const {
  if(!library_) return std::nullopt;
  const auto item = findNote(selection_.noteId);
  if(!item) return std::nullopt;
  const auto note = library_->loadNote(item->path);
  return LoadedNote {
    *item,
    note.metadata,
    note.body,
  };
}

std::optional<library::NoteListItem> AppState::findNote(std::string_view noteId) const {
  if(!organization_) return std::nullopt;
  return organization_->findNote(noteId);
}

std::optional<library::NoteListItem> AppState::createNote(const std::string& title, const std::filesystem::path& folder, std::string_view body) {
  if(!library_) return std::nullopt;
  library::NoteMetadata metadata;
  metadata.id = library::generateNoteId();
  metadata.title = uniqueTitle(*library_, title, folder);
  auto path = library_->createNote(metadata, body);
  if(!folder.empty()) path = library_->moveNote(path, folder);
  refreshLibrary();
  selection_.folder = folder;
  selection_.tag.clear();
  selection_.search.clear();
  selection_.noteId = metadata.id;
  return library::NoteListItem {metadata.id, path, metadata.title, metadata.tags};
}

bool AppState::saveSelectedNote(std::string_view body) {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  library_->saveNote(note->item.path, note->metadata, body);
  return refreshLibrary();
}

bool AppState::renameSelectedNote(const std::string& title) {
  if(!library_ || title.empty()) return false;
  auto note = selectedNote();
  if(!note) return false;
  auto metadata = note->metadata;
  metadata.title = uniqueTitle(*library_, title, note->item.path.parent_path().lexically_relative(library_->root()), note->item.path);
  const auto target = library_->renameNote(note->item.path, metadata.title);
  library_->saveNote(target, metadata, note->body);
  selection_.noteId = metadata.id;
  return refreshLibrary();
}

bool AppState::createFolder(const std::filesystem::path& folder) {
  if(!library_ || folder.empty()) return false;
  const auto target = library_->createFolder(folder);
  selection_.folder = std::filesystem::relative(target, library_->root());
  selection_.tag.clear();
  selection_.search.clear();
  selection_.noteId.clear();
  return refreshLibrary();
}

bool AppState::renameSelectedFolder(const std::filesystem::path& folder) {
  if(!library_ || selection_.folder.empty() || folder.empty()) return false;
  const auto target = library_->renameFolder(selection_.folder, folder);
  selection_.folder = std::filesystem::relative(target, library_->root());
  selection_.noteId.clear();
  return refreshLibrary();
}

bool AppState::deleteSelectedFolder() {
  if(!library_ || selection_.folder.empty()) return false;
  library_->deleteFolder(selection_.folder);
  selection_.folder.clear();
  selection_.noteId.clear();
  return refreshLibrary();
}

bool AppState::deleteSelectedNote() {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  library_->deleteNote(note->item.path);
  selection_.noteId.clear();
  return refreshLibrary();
}

bool AppState::moveSelectedNoteToFolder(const std::filesystem::path& folder) {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  const auto target = library_->moveNote(note->item.path, folder);
  selection_.folder = folder;
  selection_.tag.clear();
  selection_.search.clear();
  selection_.noteId = note->metadata.id;
  (void)target;
  return refreshLibrary();
}

bool AppState::updateSelectedTags(const std::vector<std::string>& tags) {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  note->metadata.tags = tags;
  library_->saveNote(note->item.path, note->metadata, note->body);
  return refreshLibrary();
}

bool AppState::refreshLibrary() {
  if(!library_) return false;
  organization_.emplace(*library_);
  return index_.refreshChangedFiles();
}

bool AppState::saveUiState(const std::filesystem::path& path) const {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if(!out) return false;
  out << "pane=" << static_cast<int>(shell_.paneMode) << "\n";
  out << "sidebar=" << shell_.sidebarWidth << "\n";
  out << "notelist=" << shell_.noteListWidth << "\n";
  out << "folder=" << selection_.folder.generic_string() << "\n";
  out << "tag=" << selection_.tag << "\n";
  out << "note=" << selection_.noteId << "\n";
  out << "search_scope=" << static_cast<int>(selection_.searchScope) << "\n";
  return true;
}

bool AppState::loadUiState(const std::filesystem::path& path) {
  std::ifstream in(path);
  if(!in) return false;
  std::string line;
  while(std::getline(in, line)) {
    const auto eq = line.find('=');
    if(eq == std::string::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if(key == "pane") shell_.paneMode = static_cast<PaneMode>(std::stoi(value));
    else if(key == "sidebar") shell_.sidebarWidth = std::stoi(value);
    else if(key == "notelist") shell_.noteListWidth = std::stoi(value);
    else if(key == "folder") selection_.folder = value;
    else if(key == "tag") selection_.tag = value;
    else if(key == "note") selection_.noteId = value;
    else if(key == "search_scope") selection_.searchScope = static_cast<library::SearchScope>(std::stoi(value));
  }
  return true;
}

}
