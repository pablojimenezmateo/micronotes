#include "ui/AppState.h"

#include "ui/Settings.h"
#include "ui/Theme.h"

#include "library/Metadata.h"
#include "perf/Perf.h"
#include "platform/PathUtils.h"
#include "platform/DurableFile.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>

namespace micronotes::ui {
namespace {


static std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::filesystem::path recoveryPath(const library::Library& library, std::string_view noteId) {
  return library.root() / ".micronotes" / "recovery" / (platform::sanitizeFileStem(std::string(noteId)) + ".body");
}

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

std::vector<library::NoteListItem> AppState::allNotes() const {
  if(!organization_) return {};
  return organization_->notes();
}

std::vector<library::NoteListItem> AppState::currentNotes() const {
  if(!library_ || !organization_) return {};
  if(!selection_.search.empty()) {
    std::vector<library::NoteListItem> out;
    for(const auto& result : index_.search(selection_.search, selection_.searchScope)) {
      auto item = organization_->findNote(result.id);
      if(item) out.push_back(std::move(*item));
      else out.push_back({result.id, result.path, result.title, {}, {}});
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
  return library::NoteListItem {metadata.id, path, metadata.title, metadata.tags, metadata.icon};
}

bool AppState::saveSelectedNote(std::string_view body) {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  // Adopt a note that arrived without front matter: the first save micronotes
  // performs gives it a permanent id so its identity survives a later move.
  if(note->metadata.id.empty()) {
    note->metadata.id = library::generateNoteId();
    if(note->metadata.title.empty()) note->metadata.title = note->item.title;
  }
  if(!library_->saveNote(note->item.path, note->metadata, body)) return false;
  clearSelectedNoteRecovery();
  return refreshLibrary();
}

bool AppState::saveSelectedNoteRecovery(std::string_view body) const {
  if(!library_ || selection_.noteId.empty()) return false;
  return platform::writeFileDurably(recoveryPath(*library_, selection_.noteId), body);
}

bool AppState::clearSelectedNoteRecovery() const {
  if(!library_ || selection_.noteId.empty()) return true;
  return platform::removeFileDurably(recoveryPath(*library_, selection_.noteId));
}

std::optional<std::string> AppState::selectedRecoveryBody() const {
  if(!library_ || selection_.noteId.empty()) return std::nullopt;
  const auto path = recoveryPath(*library_, selection_.noteId);
  if(!std::filesystem::exists(path)) return std::nullopt;
  return readFile(path);
}

bool AppState::renameSelectedNote(const std::string& title) {
  if(!library_ || title.empty()) return false;
  auto note = selectedNote();
  if(!note) return false;
  auto metadata = note->metadata;
  metadata.title = uniqueTitle(*library_, title, note->item.path.parent_path().lexically_relative(library_->root()), note->item.path);
  const auto target = library_->renameNote(note->item.path, metadata.title);
  if(!library_->saveNote(target, metadata, note->body)) return false;
  selection_.noteId = metadata.id;
  return refreshLibrary();
}

bool AppState::setSelectedNoteIcon(const std::string& icon) {
  if(!library_) return false;
  auto note = selectedNote();
  if(!note) return false;
  note->metadata.icon = icon;
  if(!library_->saveNote(note->item.path, note->metadata, note->body)) return false;
  return refreshLibrary();
}

bool AppState::appendToNote(std::string_view noteId, std::string_view text) {
  if(!library_) return false;
  const auto item = findNote(noteId);
  if(!item) return false;
  auto note = library_->loadNote(item->path);
  // A blank line between what was there and what arrives, or the last paragraph
  // of one note and the first of the other would become a single block.
  std::string body = note.body;
  while(!body.empty() && body.back() == '\n') body.pop_back();
  if(!body.empty()) body += "\n\n";
  body += text;
  body += "\n";
  if(!library_->saveNote(item->path, note.metadata, body)) return false;
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

bool AppState::moveFolderInto(const std::filesystem::path& folder, const std::filesystem::path& newParent) {
  if(!library_ || folder.empty()) return false;
  if(folder == newParent || folder.parent_path() == newParent) return false;
  // A folder cannot become its own child: renaming a directory into itself
  // takes it, and everything under it, out of the library.
  for(auto walk = newParent; !walk.empty(); walk = walk.parent_path()) {
    if(walk == folder) return false;
  }
  const auto target = newParent / folder.filename();
  if(std::filesystem::exists(library_->root() / target)) return false;
  library_->renameFolder(folder, target);
  if(selection_.folder == folder) selection_.folder = target;
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
  if(!library_->saveNote(note->item.path, note->metadata, note->body)) return false;
  return refreshLibrary();
}

bool AppState::favorite(std::string_view noteId) const {
  return std::find(shell_.favorites.begin(), shell_.favorites.end(), noteId) != shell_.favorites.end();
}

bool AppState::toggleFavorite(const std::string& noteId) {
  if(noteId.empty()) return false;
  const auto found = std::find(shell_.favorites.begin(), shell_.favorites.end(), noteId);
  if(found != shell_.favorites.end()) {
    shell_.favorites.erase(found);
    return false;
  }
  shell_.favorites.push_back(noteId);
  return true;
}

void AppState::noteOpened(const std::string& noteId) {
  if(noteId.empty()) return;
  auto& recents = shell_.recents;
  recents.erase(std::remove(recents.begin(), recents.end(), noteId), recents.end());
  recents.insert(recents.begin(), noteId);
  if(recents.size() > 12) recents.resize(12);
}

std::vector<library::TrashEntry> AppState::trashEntries() const {
  if(!library_) return {};
  return library_->trashEntries();
}

bool AppState::restoreFromTrash(const std::string& name) {
  if(!library_ || !library_->restoreFromTrash(name)) return false;
  return refreshLibrary();
}

bool AppState::refreshLibrary() {
  if(!library_) return false;
  organization_.emplace(*library_);
  return index_.refreshChangedFiles();
}

bool AppState::saveUiState(const std::filesystem::path& path) const {
  std::ostringstream out;
  out << "pane=" << static_cast<int>(shell_.paneMode) << "\n";
  out << "sidebar=" << shell_.sidebarWidth << "\n";
  out << "notelist=" << shell_.noteListWidth << "\n";
  out << "folder=" << selection_.folder.generic_string() << "\n";
  out << "tag=" << selection_.tag << "\n";
  out << "note=" << selection_.noteId << "\n";
  out << "search_scope=" << static_cast<int>(selection_.searchScope) << "\n";
  out << "theme=" << themeModeName(themeMode()) << "\n";
  out << "text_size=" << textSizeName(textSize()) << "\n";
  out << "page_width=" << pageWidthName(pageWidth()) << "\n";
  // One line each rather than a delimited list: a note id never contains a
  // newline, and any other separator would eventually appear inside one.
  for(const auto& id : shell_.favorites) out << "favorite=" << id << "\n";
  for(const auto& id : shell_.recents) out << "recent=" << id << "\n";
  return platform::writeFileDurably(path, out.str());
}

bool AppState::loadUiState(const std::filesystem::path& path) {
  // Cleared before the file is even opened: this is "the view state is now
  // whatever that file says", and a library with no state file of its own must
  // not inherit the favorites and the open note of the one before it.
  shell_.favorites.clear();
  shell_.recents.clear();
  selection_ = {};
  std::ifstream in(path);
  if(!in) return false;
  std::string line;
  const auto parseInt = [](const std::string& value, int fallback) {
    int result = fallback;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, result);
    if(ec != std::errc {} || ptr != last) return fallback;
    return result;
  };
  while(std::getline(in, line)) {
    const auto eq = line.find('=');
    if(eq == std::string::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if(key == "pane") {
      const int mode = parseInt(value, static_cast<int>(shell_.paneMode));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Live)) {
        shell_.paneMode = static_cast<PaneMode>(mode);
      }
    }
    else if(key == "sidebar") shell_.sidebarWidth = parseInt(value, shell_.sidebarWidth);
    else if(key == "notelist") shell_.noteListWidth = parseInt(value, shell_.noteListWidth);
    else if(key == "folder") selection_.folder = value;
    else if(key == "tag") selection_.tag = value;
    else if(key == "note") selection_.noteId = value;
    else if(key == "theme") setThemeMode(themeModeFromName(value));
    else if(key == "text_size") setTextSize(textSizeFromName(value));
    else if(key == "page_width") setPageWidth(pageWidthFromName(value));
    else if(key == "favorite" && !value.empty()) shell_.favorites.push_back(value);
    else if(key == "recent" && !value.empty()) shell_.recents.push_back(value);
    else if(key == "search_scope") {
      const int scope = parseInt(value, static_cast<int>(selection_.searchScope));
      if(scope >= static_cast<int>(library::SearchScope::All) && scope <= static_cast<int>(library::SearchScope::Content)) {
        selection_.searchScope = static_cast<library::SearchScope>(scope);
      }
    }
  }
  return true;
}

}
