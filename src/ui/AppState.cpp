#include "CoreAliases.h"
#include "ui/AppState.h"

#include "ui/Settings.h"
#include "ui/Theme.h"

#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/PathUtils.h"
#include "core/platform/DurableFile.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
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
  ++revision_;
  // The previous library's open note is not this one's.
  openNote_.reset();
  // After `ensureLayout`, so the recovery directory's parent exists, and before
  // the index opens, so no edit can be posted against the previous root.
  recovery_.setRoot(library_->root());
  // The index first: the note list is a `SELECT` over it, so it has to have
  // seen the library before anything asks for one. `organization_` is lazy, so
  // the order matters only in that nothing may read it in between.
  const bool ok = index_.open(root) && index_.refreshChangedFiles();
  organization_.emplace(*library_, index_);
  return ok;
}

bool AppState::hasLibrary() const {
  return library_.has_value();
}

const std::filesystem::path& AppState::libraryRoot() const {
  static const std::filesystem::path empty;
  return library_ ? library_->root() : empty;
}

const WorkspaceModel& AppState::workspace() const {
  return workspace_;
}

WorkspaceModel& AppState::workspace() {
  return workspace_;
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

void AppState::selectNote(std::string noteId, bool inNewTab) {
  // The tab list and the selection are one thing said twice, so they are kept
  // in step here rather than at each of the dozen call sites that open a note.
  workspace_.openNote(noteId, inNewTab);
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
  selection_.search = std::move(query);
  selection_.searchScope = scope;
}

// The empties are statics rather than temporaries: these return references, and
// a library that is not open still has to answer.
const std::vector<library::FolderNode>& AppState::folders() const {
  static const std::vector<library::FolderNode> kNone;
  return organization_ ? organization_->folders() : kNone;
}

const std::vector<std::string>& AppState::tags() const {
  static const std::vector<std::string> kNone;
  return organization_ ? organization_->tags() : kNone;
}

const std::vector<library::NoteListItem>& AppState::allNotes() const {
  static const std::vector<library::NoteListItem> kNone;
  return organization_ ? organization_->notes() : kNone;
}

std::vector<library::NoteListItem> AppState::currentNotes() const {
  if(!library_ || !organization_) return {};
  if(!selection_.search.empty()) {
    std::vector<library::NoteListItem> out;
    for(const auto& result : index_.search(selection_.search, selection_.searchScope)) {
      const auto* item = organization_->noteById(result.id);
      if(item) out.push_back(*item);
      else {
        out.push_back({result.id, result.path, result.title, {}, {},
                       result.path.lexically_relative(libraryRoot()).parent_path()});
      }
    }
    return out;
  }
  if(!selection_.tag.empty()) return organization_->notesWithTag(selection_.tag);
  return organization_->notesInFolder(selection_.folder);
}

std::vector<library::Backlink> AppState::backlinksToSelected() const {
  const auto note = findNote(selection_.noteId);
  if(!note) return {};
  // A link may name the note by its title or by its file name, and the index
  // stores whatever was written, so both spellings are asked for.
  return index_.backlinks(note->title, note->path.stem().string());
}

std::vector<library::SearchResult> AppState::currentSearchResults() const {
  if(!library_ || selection_.search.empty()) return {};
  return index_.search(selection_.search, selection_.searchScope);
}

const OpenNote& AppState::loadOpenNote(std::string* body) const {
  static const OpenNote kNone;
  if(!library_ || selection_.noteId.empty()) return kNone;
  // A hit only when the selection still names the note this was read for. The
  // library revision deliberately does not come into it: a save bumps the
  // revision, and re-reading the note micronotes has just written is exactly
  // the read this exists to avoid.
  if(openNote_ && openNote_->noteId == selection_.noteId && !body) return *openNote_;
  const library::NoteListItem* item = noteById(selection_.noteId);
  if(!item) return kNone;
  auto note = library_->loadNote(item->path);
  if(body) *body = note.body;
  OpenNote open;
  open.noteId = selection_.noteId;
  open.path = item->path;
  open.metadata = std::move(note.metadata);
  // Sampled after the read, not before. Between the two the file could change,
  // and a signature from before the read would claim agreement with bytes that
  // are no longer there -- which is the one direction that loses data.
  open.disk = platform::statFile(item->path);
  openNote_ = std::move(open);
  perf::addCounter(perf::CounterId::AppStateOpenNoteReads);
  return *openNote_;
}

const OpenNote& AppState::openNote() const {
  return loadOpenNote(nullptr);
}

std::optional<LoadedNote> AppState::readSelectedNote() const {
  std::string body;
  const OpenNote& open = loadOpenNote(&body);
  if(open.noteId.empty()) return std::nullopt;
  const library::NoteListItem* item = noteById(open.noteId);
  if(!item) return std::nullopt;
  return LoadedNote {*item, open.metadata, std::move(body)};
}

std::string_view AppState::selectedTitle() const {
  const library::NoteListItem* item = noteById(selection_.noteId);
  return item ? std::string_view(item->title) : std::string_view();
}

DiskState AppState::selectedNoteDiskState() const {
  const OpenNote& open = openNote();
  // Nothing open, or a note whose file was already absent when it was read:
  // there is no baseline to disagree with.
  if(open.noteId.empty() || !open.disk.exists) return DiskState::Agrees;
  const auto now = platform::statFile(open.path);
  if(now.error) return DiskState::Unknown;
  if(!now.exists) return DiskState::Vanished;
  return now.sameContentAs(open.disk) ? DiskState::Agrees : DiskState::Changed;
}

std::optional<library::NoteListItem> AppState::findNote(std::string_view noteId) const {
  if(!organization_) return std::nullopt;
  return organization_->findNote(noteId);
}

const library::NoteListItem* AppState::noteById(std::string_view noteId) const {
  return organization_ ? organization_->noteById(noteId) : nullptr;
}

std::optional<library::NoteListItem> AppState::createNote(const std::string& title, const std::filesystem::path& folder, std::string_view body) {
  if(!library_) return std::nullopt;
  library::NoteMetadata metadata;
  metadata.id = library::generateNoteId();
  metadata.title = uniqueTitle(*library_, title, folder);
  auto path = library_->createNote(metadata, body);
  if(!folder.empty()) path = library_->moveNote(path, folder);
  openNote_.reset();
  refreshLibrary();
  selection_.folder = folder;
  selection_.tag.clear();
  selection_.search.clear();
  selection_.noteId = metadata.id;
  workspace_.openNote(metadata.id, false);
  return library::NoteListItem {metadata.id,   path,          metadata.title,
                                metadata.tags, metadata.icon,
                                path.lexically_relative(library_->root()).parent_path()};
}

SaveResult AppState::saveSelectedNote(std::string_view body) {
  perf::ScopeTimer timer("app_state.save_selected_note");
  SaveResult result;
  if(!library_) return result;
  const OpenNote& open = openNote();
  if(open.noteId.empty()) return result;
  const auto path = open.path;

  // Has anything else rewritten the file since it was read? One stat, and the
  // difference between a save and a silent overwrite of somebody's `git
  // checkout`. `Vanished` is not a conflict: the buffer is the only copy left,
  // and writing it back is the whole point.
  if(selectedNoteDiskState() != DiskState::Agrees && platform::statFile(path).exists) {
    perf::addCounter(perf::CounterId::AppStateSaveConflicts);
    result.conflictCopy = library_->preserveExternalVersion(path);
    // Could not put it somewhere safe, so it does not get destroyed either. The
    // buffer is still queued in the recovery store, so refusing loses nothing.
    if(result.conflictCopy.empty()) return result;
  }

  library::NoteMetadata metadata = open.metadata;
  // Adopt a note that arrived without front matter: the first save micronotes
  // performs gives it a permanent id so its identity survives a later move.
  const bool adoptedId = metadata.id.empty();
  if(adoptedId) {
    metadata.id = library::generateNoteId();
    if(metadata.title.empty()) {
      const library::NoteListItem* item = noteById(open.noteId);
      metadata.title = item ? item->title : path.stem().string();
    }
  }
  if(!library_->saveNote(path, metadata, body)) return result;
  result.ok = true;

  // The record is now the truth about the file that was just written, so
  // nothing has to go back to the disk to find out what its front matter says.
  openNote_->metadata = std::move(metadata);
  openNote_->disk = platform::statFile(path);

  clearSelectedNoteRecovery();

  // A save knows which file it wrote *and* what it wrote into it, so
  // re-indexing that one file is a stat and one transaction -- no walk, no
  // whole-table read, and not even a read of the note. Discovering the same
  // thing by rescanning the library was most of what a save cost.
  bool listChanged = refreshWrittenNoteFile(path, openNote_->metadata, body);
  if(!result.conflictCopy.empty()) {
    // The rescued version is a note now, so it has to be indexed as one -- a
    // note the user cannot find in the sidebar is a note they will not find at
    // all, whatever the status line said. This one is read rather than handed
    // over: `preserveExternalVersion` wrote somebody else's bytes, not ours.
    refreshNoteFile(path.parent_path() / result.conflictCopy);
    listChanged = true;
  }

  if(adoptedId) {
    // The note's identity changed with its front matter: it was filed under an
    // id derived from its path and now carries a permanent one. Everything
    // pointing at the old id -- the selection, its tab, the favorites, the
    // recents -- has to follow, or the note the user is looking at disappears
    // out from under them on the first save.
    const std::string previous = std::move(openNote_->noteId);
    const std::string& adopted = openNote_->metadata.id;
    // The recovery copy filed under the old id was already retired by the
    // `clearSelectedNoteRecovery` above, which ran while the selection still
    // named it -- so nothing is left behind under a name nothing answers to.
    workspace_.renameNote(previous, adopted);
    std::replace(workspace_.favorites.begin(), workspace_.favorites.end(), previous, adopted);
    std::replace(workspace_.recents.begin(), workspace_.recents.end(), previous, adopted);
    selection_.noteId = adopted;
    openNote_->noteId = adopted;
  }
  if(listChanged || adoptedId) invalidateNoteList();
  return result;
}

bool AppState::saveSelectedNoteHeader(const library::NoteMetadata& metadata) {
  const auto note = readSelectedNote();
  if(!note) return false;
  if(!library_->saveNote(note->item.path, metadata, note->body)) return false;
  openNote_->metadata = metadata;
  openNote_->disk = platform::statFile(note->item.path);
  refreshWrittenNoteFile(note->item.path, metadata, note->body);
  invalidateNoteList();
  return true;
}

bool AppState::saveSelectedNoteRecovery(std::string_view body) const {
  if(!library_ || selection_.noteId.empty()) return false;
  return recovery_.save(selection_.noteId, body);
}

bool AppState::clearSelectedNoteRecovery() const {
  if(!library_ || selection_.noteId.empty()) return true;
  return recovery_.clear(selection_.noteId);
}

std::optional<std::string> AppState::selectedRecoveryBody() const {
  if(!library_ || selection_.noteId.empty()) return std::nullopt;
  return recovery_.read(selection_.noteId);
}

bool AppState::renameSelectedNote(const std::string& title) {
  if(!library_ || title.empty()) return false;
  auto note = readSelectedNote();
  if(!note) return false;
  auto metadata = note->metadata;
  metadata.title = uniqueTitle(*library_, title, note->item.folder, note->item.path);
  const auto source = note->item.path;
  // One write. This used to call `renameNote`, which read the note back off the
  // disk, patched the title it had just been given, and wrote it -- and then
  // wrote the whole note a second time with the metadata here. Four fsync
  // barriers and a redundant whole-file read for one rename, and two writes
  // with no stated rule about which of them won.
  const auto target = library_->saveNoteAs(source, metadata, note->body);
  if(target.empty()) return false;
  selection_.noteId = metadata.id;
  // The file moved, so the record's path did too.
  openNote_->noteId = metadata.id;
  openNote_->path = target;
  openNote_->metadata = std::move(metadata);
  openNote_->disk = platform::statFile(target);
  // Both ends of the move: the row under the old name has to go, and the row
  // under the new one has to appear.
  if(target != source) refreshNoteFile(source);
  refreshNoteFile(target);
  invalidateNoteList();
  return true;
}

bool AppState::setSelectedNoteIcon(const std::string& icon) {
  if(!library_) return false;
  library::NoteMetadata metadata = openNote().metadata;
  if(openNote().noteId.empty()) return false;
  metadata.icon = icon;
  return saveSelectedNoteHeader(metadata);
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
  // The target is not the open note, so nothing cached here describes it -- and
  // appending to a body changes nothing the note list shows.
  const auto path = item->path;
  refreshNoteFile(path);
  return true;
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
  const OpenNote& open = openNote();
  if(open.noteId.empty()) return false;
  const auto path = open.path;
  library_->deleteNote(path);
  openNote_.reset();
  // A deleted note must not be left open in a tab pointing at nothing; closing
  // it also chooses what to show next.
  if(const auto tab = workspace_.findTab(selection_.noteId); tab != std::string::npos) {
    closeTab(tab);
  } else {
    selection_.noteId.clear();
  }
  // The file is gone rather than changed, which `refreshFile` handles: it drops
  // the rows a path no longer backs.
  refreshNoteFile(path);
  invalidateNoteList();
  return true;
}

bool AppState::moveSelectedNoteToFolder(const std::filesystem::path& folder) {
  if(!library_) return false;
  const OpenNote& open = openNote();
  if(open.noteId.empty()) return false;
  const auto noteId = open.metadata.id;
  library_->moveNote(open.path, folder);
  selection_.folder = folder;
  selection_.tag.clear();
  selection_.search.clear();
  if(!noteId.empty()) selection_.noteId = noteId;
  // The full walk, not `refreshNoteFile`: the move may have created the folder,
  // and the sidebar tree is drawn from the directories the walk reports --
  // including the empty ones, which no list of notes can name.
  openNote_.reset();
  return refreshLibrary();
}

bool AppState::updateSelectedTags(const std::vector<std::string>& tags) {
  if(!library_) return false;
  library::NoteMetadata metadata = openNote().metadata;
  if(openNote().noteId.empty()) return false;
  metadata.tags = tags;
  return saveSelectedNoteHeader(metadata);
}

bool AppState::favorite(std::string_view noteId) const {
  return std::find(workspace_.favorites.begin(), workspace_.favorites.end(), noteId) != workspace_.favorites.end();
}

bool AppState::toggleFavorite(const std::string& noteId) {
  if(noteId.empty()) return false;
  const auto found = std::find(workspace_.favorites.begin(), workspace_.favorites.end(), noteId);
  if(found != workspace_.favorites.end()) {
    workspace_.favorites.erase(found);
    return false;
  }
  workspace_.favorites.push_back(noteId);
  return true;
}

void AppState::noteOpened(const std::string& noteId) {
  if(noteId.empty()) return;
  auto& recents = workspace_.recents;
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
  ++revision_;
  const bool ok = index_.refreshChangedFiles();
  // Emplaced after the refresh, for the reason `openOrCreateLibrary` gives: the
  // note list reads the index, so the index has to have seen the change first.
  organization_.emplace(*library_, index_);
  return ok;
}

bool AppState::refreshNoteFile(const std::filesystem::path& path) {
  if(!library_) return false;
  return applied(index_.refreshFile(path));
}

bool AppState::refreshWrittenNoteFile(const std::filesystem::path& path,
                                      const library::NoteMetadata& metadata,
                                      std::string_view body) {
  if(!library_) return false;
  return applied(index_.refreshWrittenFile(path, metadata, body));
}

bool AppState::applied(const library::LibraryIndex::FileRefresh& refresh) {
  // The revision moves whether or not the *note list* did, because it is what
  // every view memo is keyed on and the things derived from a note's body --
  // its backlinks, the search results it appears in -- change on a save that
  // touches none of the five fields the list is built from.
  //
  // But only when a row actually moved. A file whose stat still matches its row
  // is every echo of micronotes' own save arriving back through the watcher,
  // and bumping the revision for one of those would rebuild the sidebar, the
  // header and the backlinks panel to show exactly what they were showing.
  if(refresh.rowsWritten) ++revision_;
  return refresh.listFieldsChanged;
}

bool AppState::reloadSelectedNote() {
  const OpenNote& open = openNote();
  if(open.noteId.empty()) return false;
  const auto path = open.path;
  const auto previous = open.noteId;
  openNote_.reset();
  if(refreshNoteFile(path)) invalidateNoteList();
  if(noteById(previous)) return true;
  // The row moved out from under the selection, which happens when what changed
  // on disk was the front matter's `id`. The file is the same file, so the note
  // it now answers to is found by path and everything pointing at the old id
  // follows it.
  for(const auto& note : allNotes()) {
    if(note.path != path) continue;
    workspace_.renameNote(previous, note.id);
    std::replace(workspace_.favorites.begin(), workspace_.favorites.end(), previous, note.id);
    std::replace(workspace_.recents.begin(), workspace_.recents.end(), previous, note.id);
    selection_.noteId = note.id;
    return true;
  }
  return true;
}

void AppState::invalidateNoteList() {
  if(!library_) return;
  // Re-emplaced rather than asked to forget: the memos are the whole of its
  // state, so a fresh one is the cheapest possible invalidation and there is no
  // second code path holding a half-cleared service.
  organization_.emplace(*library_, index_);
}

std::uint64_t AppState::revision() const {
  return revision_;
}

bool AppState::saveUiState(const std::filesystem::path& path) const {
  std::ostringstream out;
  // Written for a version that predates tabs: it reads the pane mode and the
  // open note from these two and gets a working single-tab session. Keys are
  // added rather than redefined, so an older binary keeps working too.
  out << "pane=" << static_cast<int>(workspace_.paneMode()) << "\n";
  for(const auto& tab : workspace_.tabs) {
    out << "tab=" << static_cast<int>(tab.paneMode) << "|" << (tab.pinned ? 1 : 0) << "|" << tab.noteId << "\n";
  }
  out << "active_tab=" << workspace_.activeTab << "\n";
  // Rounded on the way out: an older binary parses these with from_chars into
  // an int, and "240.5" would make it fall back to its default instead.
  out << "sidebar=" << static_cast<int>(std::lround(workspace_.sidebarWidth)) << "\n";
  out << "rightpanel=" << static_cast<int>(std::lround(workspace_.rightPanelWidth)) << "\n";
  out << "panel_sidebar=" << (workspace_.sidebarVisible ? 1 : 0) << "\n";
  out << "panel_right=" << (workspace_.rightPanelVisible ? 1 : 0) << "\n";
  out << "right_view=" << rightPanelViewName(workspace_.rightPanelView) << "\n";
  out << "folder=" << selection_.folder.generic_string() << "\n";
  out << "tag=" << selection_.tag << "\n";
  out << "note=" << selection_.noteId << "\n";
  out << "search_scope=" << static_cast<int>(selection_.searchScope) << "\n";
  out << "theme=" << themeModeName(themeMode()) << "\n";
  out << "text_size=" << textSizeName(textSize()) << "\n";
  out << "page_width=" << pageWidthName(pageWidth()) << "\n";
  // One line each rather than a delimited list: a note id never contains a
  // newline, and any other separator would eventually appear inside one.
  for(const auto& id : workspace_.favorites) out << "favorite=" << id << "\n";
  for(const auto& id : workspace_.recents) out << "recent=" << id << "\n";
  return platform::writeFileDurably(path, out.str());
}

bool AppState::loadUiState(const std::filesystem::path& path) {
  // Cleared before the file is even opened: this is "the view state is now
  // whatever that file says", and a library with no state file of its own must
  // not inherit the favorites and the open note of the one before it.
  workspace_.favorites.clear();
  workspace_.recents.clear();
  // A file written before panels could be hidden says nothing about them, and
  // the arrangement it was written under is the one the defaults describe.
  workspace_.sidebarVisible = true;
  workspace_.rightPanelVisible = false;
  workspace_.tabs.clear();
  workspace_.activeTab = 0;
  selection_ = {};
  std::ifstream in(path);
  if(!in) return false;
  std::string line;
  // A file written before tabs existed carries `pane=` and `note=` instead; one
  // tab is synthesised from them below rather than the session opening empty.
  std::optional<PaneMode> legacyPane;
  int activeTab = 0;
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
      const int mode = parseInt(value, static_cast<int>(PaneMode::Live));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Live)) {
        legacyPane = static_cast<PaneMode>(mode);
      }
    }
    else if(key == "tab") {
      // "<pane>|<pinned>|<note id>". The id is last and unsplit, because it is
      // the only field that could contain a separator.
      const auto firstBar = value.find('|');
      const auto secondBar = firstBar == std::string::npos ? std::string::npos : value.find('|', firstBar + 1);
      if(secondBar == std::string::npos) continue;
      NoteTab tab;
      const int mode = parseInt(value.substr(0, firstBar), static_cast<int>(PaneMode::Live));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Live)) {
        tab.paneMode = static_cast<PaneMode>(mode);
      }
      tab.pinned = parseInt(value.substr(firstBar + 1, secondBar - firstBar - 1), 0) != 0;
      tab.noteId = value.substr(secondBar + 1);
      if(!tab.noteId.empty()) workspace_.tabs.push_back(std::move(tab));
    }
    else if(key == "active_tab") activeTab = parseInt(value, 0);
    else if(key == "sidebar") workspace_.sidebarWidth = static_cast<float>(parseInt(value, static_cast<int>(workspace_.sidebarWidth)));
    else if(key == "rightpanel") workspace_.rightPanelWidth = static_cast<float>(parseInt(value, static_cast<int>(workspace_.rightPanelWidth)));
    else if(key == "panel_sidebar") workspace_.sidebarVisible = parseInt(value, 1) != 0;
    else if(key == "panel_right") workspace_.rightPanelVisible = parseInt(value, 0) != 0;
    else if(key == "right_view") workspace_.rightPanelView = rightPanelViewFromName(value);
    else if(key == "folder") selection_.folder = value;
    else if(key == "tag") selection_.tag = value;
    else if(key == "note") selection_.noteId = value;
    else if(key == "theme") setThemeMode(themeModeFromName(value));
    else if(key == "text_size") setTextSize(textSizeFromName(value));
    else if(key == "page_width") setPageWidth(pageWidthFromName(value));
    else if(key == "favorite" && !value.empty()) workspace_.favorites.push_back(value);
    else if(key == "recent" && !value.empty()) workspace_.recents.push_back(value);
    else if(key == "search_scope") {
      const int scope = parseInt(value, static_cast<int>(selection_.searchScope));
      if(scope >= static_cast<int>(library::SearchScope::All) && scope <= static_cast<int>(library::SearchScope::Content)) {
        selection_.searchScope = static_cast<library::SearchScope>(scope);
      }
    }
  }
  if(workspace_.tabs.empty() && !selection_.noteId.empty()) {
    NoteTab tab;
    tab.noteId = selection_.noteId;
    tab.paneMode = legacyPane.value_or(PaneMode::Live);
    workspace_.tabs.push_back(std::move(tab));
  } else if(legacyPane && !workspace_.tabs.empty()) {
    // A file with tabs was written by a version that has them, so `pane=` is
    // only the legacy echo of the active tab and must not override it.
  }
  workspace_.activeTab = workspace_.tabs.empty()
    ? 0
    : std::min(static_cast<std::size_t>(std::max(activeTab, 0)), workspace_.tabs.size() - 1);
  // The active tab is what is open, whatever the legacy `note=` line said.
  if(const auto* tab = workspace_.activeTab_()) selection_.noteId = tab->noteId;
  return true;
}

}
