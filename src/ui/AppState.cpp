#include "CoreAliases.h"
#include "ui/AppState.h"

#include "ui/UiStateFile.h"

#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/DurableFile.h"

#include <algorithm>
#include <utility>

namespace micronotes::ui {

bool AppState::openOrCreateLibrary(const std::filesystem::path& root) {
  perf::ScopeTimer timer("app_state.open_or_create_library");
  // The previous library's open note is not this one's, and a draft posted
  // against the previous root must not be filed under this one.
  openNote_.forget();
  const bool ok = catalog_.open(root);
  openNote_.setRecoveryRoot(catalog_.root());
  return ok;
}

const library::NoteCatalog& AppState::catalog() const {
  return catalog_;
}

const OpenNoteRecord& AppState::openNote() const {
  return openNote_;
}

const WorkspaceModel& AppState::workspace() const {
  return workspace_;
}

WorkspaceModel& AppState::editWorkspace() {
  return workspace_;
}

const UiSelection& AppState::selection() const {
  return selection_;
}

void AppState::selectFolder(std::filesystem::path folder) {
  selection_.showFolder(std::move(folder));
}

void AppState::selectTag(std::string tag) {
  selection_.showTag(std::move(tag));
}

void AppState::selectNote(std::string noteId, ui::TabPolicy policy) {
  // The tab list and the selection are one thing said twice, so they are kept
  // in step here rather than at each of the dozen call sites that open a note.
  workspace_.openNote(noteId, policy);
  selection_.noteId = std::move(noteId);
}

void AppState::closeTab(std::size_t index) {
  workspace_.closeTab(index);
  const auto* tab = workspace_.activeTab_();
  selection_.noteId = tab ? tab->noteId : std::string();
}

void AppState::stepTab(int delta) {
  workspace_.stepTab(delta);
  if(const auto* tab = workspace_.activeTab_()) selection_.noteId = tab->noteId;
}

void AppState::setSearch(std::string query, library::SearchScope scope) {
  selection_.showSearch(std::move(query), scope);
}

std::vector<library::NoteListItem> AppState::currentNotes() const {
  if(!catalog_.isOpen()) return {};
  if(!selection_.search.empty()) {
    std::vector<library::NoteListItem> out;
    for(const auto& result : catalog_.search(selection_.search, selection_.searchScope)) {
      if(const auto* item = catalog_.noteById(result.id)) out.push_back(*item);
      else {
        out.push_back({result.id, result.path, result.title, {}, {},
                       result.path.lexically_relative(catalog_.root()).parent_path()});
      }
    }
    return out;
  }
  if(!selection_.tag.empty()) return catalog_.notesWithTag(selection_.tag);
  return catalog_.notesInFolder(selection_.folder);
}

std::vector<library::SearchResult> AppState::currentSearchResults() const {
  return catalog_.search(selection_.search, selection_.searchScope);
}

std::vector<library::Backlink> AppState::backlinksToSelected() const {
  const library::NoteListItem* note = catalog_.noteById(selection_.noteId);
  if(!note) return {};
  // A link may name the note by its title or by its file name, and the index
  // stores whatever was written, so both spellings are asked for.
  return catalog_.backlinks(note->title, note->path.stem().string());
}

std::string_view AppState::selectedTitle() const {
  const library::NoteListItem* item = catalog_.noteById(selection_.noteId);
  return item ? std::string_view(item->title) : std::string_view();
}

void AppState::adoptIdIfMissing(library::NoteMetadata& metadata) const {
  if(!metadata.id.empty()) return;
  const OpenNote& open = openNote_.get();
  metadata.id = library::generateNoteId();
  if(!metadata.title.empty()) return;
  const library::NoteListItem* item = catalog_.noteById(open.noteId);
  metadata.title = item ? item->title : open.path.stem().string();
}

void AppState::followIdChange(const std::string& previous, const std::string& adopted) {
  workspace_.renameNote(previous, adopted);
  selection_.noteId = adopted;
  openNote_.adoptId(adopted);
}

SaveResult AppState::writeOpenNote(library::NoteMetadata metadata, std::string_view body,
                                   NamePolicy naming) {
  SaveResult result;
  if(!catalog_.isOpen()) return result;
  const OpenNote& open = openNote_.get();
  if(open.noteId.empty()) return result;
  const auto source = open.path;
  const std::string previousId = open.noteId;
  const std::string adoptedId = metadata.id;

  // Has anything else rewritten the file since it was read? One stat, and the
  // difference between a save and a silent overwrite of somebody's `git
  // checkout`. `Vanished` is not a conflict: the buffer is the only copy left,
  // and writing it back is the whole point.
  if(openNote_.diskState() != DiskState::Agrees && platform::statFile(source).exists) {
    perf::addCounter(perf::CounterId::AppStateSaveConflicts);
    result.conflictCopy = catalog_.preserveExternalVersion(source);
    // Could not put it somewhere safe, so it does not get destroyed either. The
    // buffer is still queued in the recovery store, so refusing loses nothing.
    if(result.conflictCopy.empty()) return result;
  }

  auto target = source;
  if(naming == NamePolicy::FollowTitle) {
    // One write. This used to call `renameNote`, which read the note back off
    // the disk, patched the title it had just been given, and wrote it -- and
    // then wrote the whole note a second time with the metadata here. Four
    // fsync barriers and a redundant whole-file read for one rename, and two
    // writes with no stated rule about which of them won.
    target = catalog_.writeNoteAs(source, metadata, body);
    if(target.empty()) return result;
  } else if(!catalog_.writeNote(source, metadata, body)) {
    return result;
  }
  result.ok = true;

  // The record is now the truth about the file that was just written, so
  // nothing has to go back to the disk to find out what its front matter says.
  openNote_.recordWrite(target, std::move(metadata));
  // Retired while the selection still names the note, so nothing is left behind
  // under an id nothing answers to.
  openNote_.clearRecovery();
  if(adoptedId != previousId) followIdChange(previousId, adoptedId);
  return result;
}

SaveResult AppState::saveSelectedNote(std::string_view body) {
  perf::ScopeTimer timer("app_state.save_selected_note");
  library::NoteMetadata metadata = openNote_.get().metadata;
  adoptIdIfMissing(metadata);
  return writeOpenNote(std::move(metadata), body, NamePolicy::Keep);
}

bool AppState::renameSelectedNote(const std::string& title, std::string_view body) {
  if(!catalog_.isOpen() || title.empty()) return false;
  const OpenNote& open = openNote_.get();
  if(open.noteId.empty()) return false;
  library::NoteMetadata metadata = open.metadata;
  adoptIdIfMissing(metadata);
  const library::NoteListItem* item = catalog_.noteById(open.noteId);
  metadata.title = catalog_.uniqueTitle(
      title, item ? item->folder : std::filesystem::path {}, open.path);
  return writeOpenNote(std::move(metadata), body, NamePolicy::FollowTitle).ok;
}

bool AppState::setSelectedNoteIcon(const std::string& icon, std::string_view body) {
  library::NoteMetadata metadata = openNote_.get().metadata;
  adoptIdIfMissing(metadata);
  metadata.icon = icon;
  return writeOpenNote(std::move(metadata), body, NamePolicy::Keep).ok;
}

bool AppState::updateSelectedTags(const std::vector<std::string>& tags, std::string_view body) {
  library::NoteMetadata metadata = openNote_.get().metadata;
  adoptIdIfMissing(metadata);
  metadata.tags = tags;
  return writeOpenNote(std::move(metadata), body, NamePolicy::Keep).ok;
}

std::size_t AppState::removeTagEverywhere(std::string_view tag, std::string_view openBody) {
  if(tag.empty() || !catalog_.isOpen()) return 0;
  // The paths are copied out first: every write below drops the note list the
  // borrow would point into, so walking it as we go would walk a freed vector.
  std::vector<std::pair<std::string, std::filesystem::path>> carrying;
  for(const auto& note : catalog_.notes()) {
    if(std::find(note.tags.begin(), note.tags.end(), tag) == note.tags.end()) continue;
    carrying.push_back({note.id, note.path});
  }

  const std::string openId = openNote_.noteId();
  std::size_t changed = 0;
  for(const auto& [id, path] : carrying) {
    if(id == openId) {
      // Through the guarded write, with the buffer's body. This is the one note
      // whose file on disk may be behind what is on screen, and rewriting it
      // from the disk would throw away everything typed since the last save.
      library::NoteMetadata metadata = openNote_.metadata();
      std::erase(metadata.tags, tag);
      if(writeOpenNote(std::move(metadata), openBody, NamePolicy::Keep).ok) ++changed;
      continue;
    }
    auto note = catalog_.loadNote(path);
    std::erase(note.metadata.tags, tag);
    if(catalog_.writeNote(path, note.metadata, note.body)) ++changed;
  }
  // The tag was a filter as well as a label, and it no longer names anything.
  if(selection_.tag == tag) selection_.showFolder({});
  return changed;
}

std::optional<library::NoteListItem> AppState::createNote(const std::string& title,
                                                          const std::filesystem::path& folder,
                                                          std::string_view body,
                                                          ui::TabPolicy policy) {
  auto item = catalog_.createNote(title, folder, body);
  if(!item) return std::nullopt;
  openNote_.forget();
  selection_.showFolder(folder);
  selection_.noteId = item->id;
  workspace_.openNote(item->id, policy);
  return item;
}

bool AppState::appendToNote(std::string_view noteId, std::string_view text) {
  const library::NoteListItem* item = catalog_.noteById(noteId);
  if(!item) return false;
  // Copied out before the write: the item is a borrow into the note list's
  // memo, and writing drops it.
  const auto path = item->path;
  auto note = catalog_.loadNote(path);
  // A blank line between what was there and what arrives, or the last paragraph
  // of one note and the first of the other would become a single block.
  std::string body = std::move(note.body);
  while(!body.empty() && body.back() == '\n') body.pop_back();
  if(!body.empty()) body += "\n\n";
  body += text;
  body += "\n";
  // The target is not the open note, so nothing memoised here describes it --
  // and appending to a body changes nothing the note list shows.
  return catalog_.writeNote(path, note.metadata, body);
}

bool AppState::deleteSelectedNote() {
  const OpenNote& open = openNote_.get();
  if(open.noteId.empty()) return false;
  const auto path = open.path;
  // The recovery copy goes with it, and while the selection still names it.
  // Nothing will ever ask about this id again, and a `.body` file left behind
  // under it is an offer to restore a draft of a note that no longer exists.
  openNote_.clearRecovery();
  catalog_.deleteNote(path);
  openNote_.forget();
  // A deleted note must not be left open in a tab pointing at nothing; closing
  // it also chooses what to show next.
  if(const auto tab = workspace_.findTab(selection_.noteId); tab != std::string::npos) {
    closeTab(tab);
  } else {
    selection_.noteId.clear();
  }
  return true;
}

bool AppState::moveSelectedNoteToFolder(const std::filesystem::path& folder) {
  const OpenNote& open = openNote_.get();
  if(open.noteId.empty()) return false;
  const auto path = open.path;
  const auto noteId = open.metadata.id;
  openNote_.forget();
  if(catalog_.moveNote(path, folder).empty()) return false;
  selection_.showFolder(folder);
  if(!noteId.empty()) selection_.noteId = noteId;
  return true;
}

bool AppState::createFolder(const std::filesystem::path& folder) {
  if(!catalog_.isOpen() || folder.empty()) return false;
  const auto target = catalog_.createFolder(folder);
  // Refused by the catalog: a notebook called `files`, or one under a files
  // directory, would not be a notebook. See `library::kFilesDirName`.
  if(target.empty()) return false;
  selection_.showFolder(std::filesystem::relative(target, catalog_.root()));
  selection_.noteId.clear();
  return true;
}

bool AppState::renameSelectedFolder(const std::filesystem::path& folder) {
  if(!catalog_.isOpen() || selection_.folder.empty() || folder.empty()) return false;
  const auto target = catalog_.renameFolder(selection_.folder, folder);
  if(target.empty()) return false;
  selection_.showFolder(std::filesystem::relative(target, catalog_.root()));
  selection_.noteId.clear();
  return true;
}

bool AppState::moveFolderInto(const std::filesystem::path& folder,
                              const std::filesystem::path& newParent) {
  if(!catalog_.isOpen() || folder.empty()) return false;
  if(folder == newParent || folder.parent_path() == newParent) return false;
  // A notebook dropped into a files directory would stop being one.
  if(library::insideFilesDir(newParent)) return false;
  // A folder cannot become its own child: renaming a directory into itself
  // takes it, and everything under it, out of the library.
  for(auto walk = newParent; !walk.empty(); walk = walk.parent_path()) {
    if(walk == folder) return false;
  }
  const auto target = newParent / folder.filename();
  if(std::filesystem::exists(catalog_.root() / target)) return false;
  catalog_.renameFolder(folder, target);
  if(selection_.folder == folder) selection_.showFolder(target);
  return true;
}

bool AppState::deleteSelectedFolder() {
  if(!catalog_.isOpen() || selection_.folder.empty()) return false;
  catalog_.deleteFolder(selection_.folder);
  selection_.showFolder({});
  selection_.noteId.clear();
  return true;
}

bool AppState::restoreFromTrash(const std::string& name) {
  return catalog_.restoreFromTrash(name);
}

std::filesystem::path AppState::renameCompanion(const std::filesystem::path& relative,
                                                const std::string& newName) {
  return catalog_.renameCompanion(relative, newName);
}

std::filesystem::path AppState::moveCompanion(const std::filesystem::path& relative,
                                              const std::filesystem::path& destinationDir) {
  return catalog_.moveCompanion(relative, destinationDir);
}

bool AppState::deleteCompanion(const std::filesystem::path& relative) {
  return catalog_.deleteCompanion(relative);
}

std::filesystem::path AppState::createCompanionFolder(const std::filesystem::path& relative) {
  return catalog_.createCompanionFolder(relative);
}

void AppState::refreshFilesDir(const std::filesystem::path& filesDir) {
  catalog_.refreshFilesDir(filesDir);
}

bool AppState::refreshLibrary() {
  return catalog_.refresh();
}

bool AppState::refreshNoteFile(const std::filesystem::path& path) {
  return catalog_.refreshFile(path);
}

bool AppState::reloadSelectedNote() {
  const OpenNote& open = openNote_.get();
  if(open.noteId.empty()) return false;
  const auto path = open.path;
  const auto previous = open.noteId;
  openNote_.forget();
  catalog_.refreshFile(path);
  if(catalog_.noteById(previous)) return true;
  // The row moved out from under the selection, which happens when what changed
  // on disk was the front matter's `id`. The file is the same file, so the note
  // it now answers to is found by path and everything pointing at the old id
  // follows it.
  for(const auto& note : catalog_.notes()) {
    if(note.path != path) continue;
    followIdChange(previous, note.id);
    return true;
  }
  return true;
}

bool AppState::saveUiState(const std::filesystem::path& path) const {
  return writeUiState(path, workspace_, selection_);
}

bool AppState::loadUiState(const std::filesystem::path& path) {
  return readUiState(path, workspace_, selection_);
}

}
