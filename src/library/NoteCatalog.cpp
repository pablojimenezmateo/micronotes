#include "CoreAliases.h"
#include "library/NoteCatalog.h"

#include "library/Metadata.h"
#include "core/perf/Perf.h"
#include "core/platform/PathUtils.h"

#include <utility>

namespace micronotes::library {

bool NoteCatalog::open(const std::filesystem::path& root) {
  perf::ScopeTimer timer("note_catalog.open");
  library_.emplace(root);
  library_->ensureLayout();
  ++revision_;
  // The index first: the note list is a `SELECT` over it, so it has to have
  // seen the library before anything asks for one. `organization_` is lazy, so
  // the order matters only in that nothing may read it in between.
  const bool ok = index_.open(root) && index_.refreshChangedFiles();
  organization_.emplace(*library_, index_);
  return ok;
}

bool NoteCatalog::isOpen() const {
  return library_.has_value();
}

const std::filesystem::path& NoteCatalog::root() const {
  static const std::filesystem::path kNone;
  return library_ ? library_->root() : kNone;
}

std::uint64_t NoteCatalog::revision() const {
  return revision_;
}

// The empties are statics rather than temporaries: these return references, and
// a library that is not open still has to answer.
const std::vector<NoteListItem>& NoteCatalog::notes() const {
  static const std::vector<NoteListItem> kNone;
  return organization_ ? organization_->notes() : kNone;
}

const std::vector<FolderNode>& NoteCatalog::folders() const {
  static const std::vector<FolderNode> kNone;
  return organization_ ? organization_->folders() : kNone;
}

const std::vector<std::string>& NoteCatalog::tags() const {
  static const std::vector<std::string> kNone;
  return organization_ ? organization_->tags() : kNone;
}

std::vector<NoteListItem> NoteCatalog::notesInFolder(const std::filesystem::path& folder) const {
  return organization_ ? organization_->notesInFolder(folder) : std::vector<NoteListItem> {};
}

std::vector<NoteListItem> NoteCatalog::notesWithTag(const std::string& tag) const {
  return organization_ ? organization_->notesWithTag(tag) : std::vector<NoteListItem> {};
}

std::optional<NoteListItem> NoteCatalog::findNote(std::string_view noteId) const {
  if(!organization_) return std::nullopt;
  return organization_->findNote(noteId);
}

const NoteListItem* NoteCatalog::noteById(std::string_view noteId) const {
  return organization_ ? organization_->noteById(noteId) : nullptr;
}

std::vector<SearchResult> NoteCatalog::search(std::string_view query, SearchScope scope) const {
  if(!library_ || query.empty()) return {};
  return index_.search(query, scope);
}

std::vector<Backlink> NoteCatalog::backlinks(std::string_view title, std::string_view stem) const {
  if(!library_) return {};
  return index_.backlinks(title, stem);
}

std::vector<TrashEntry> NoteCatalog::trashEntries() const {
  if(!library_) return {};
  return library_->trashEntries();
}

LoadedNote NoteCatalog::loadNote(const std::filesystem::path& path) const {
  if(!library_) return {};
  return library_->loadNote(path);
}

std::string NoteCatalog::uniqueTitle(const std::string& requested,
                                     const std::filesystem::path& folder,
                                     const std::filesystem::path& keep) const {
  const auto base = requested.empty() ? "Untitled" : requested;
  if(!library_) return base;
  std::string candidate = base;
  const auto targetDir = folder.empty() ? library_->root() : library_->root() / folder;
  int suffix = 2;
  while(true) {
    const auto candidatePath = targetDir / (platform::sanitizeFileStem(candidate) + ".md");
    if(!std::filesystem::exists(candidatePath)
       || (!keep.empty() && std::filesystem::equivalent(candidatePath, keep))) break;
    candidate = base + "-" + std::to_string(suffix++);
  }
  return candidate;
}

bool NoteCatalog::writeNote(const std::filesystem::path& path, const NoteMetadata& metadata,
                            std::string_view body) {
  if(!library_ || !library_->saveNote(path, metadata, body)) return false;
  // A save knows which file it wrote *and* what it wrote into it, so
  // re-indexing that one file is a stat and one transaction -- no walk, no
  // whole-table read, and not even a read of the note back off the disk.
  applied(index_.refreshWrittenFile(path, metadata, body));
  return true;
}

std::filesystem::path NoteCatalog::writeNoteAs(const std::filesystem::path& path,
                                               const NoteMetadata& metadata,
                                               std::string_view body) {
  if(!library_) return {};
  const auto target = library_->saveNoteAs(path, metadata, body);
  if(target.empty()) return target;
  // Both ends of the move: the row under the old name has to go, and the row
  // under the new one has to appear.
  if(target != path) refreshFile(path);
  applied(index_.refreshWrittenFile(target, metadata, body));
  return target;
}

std::optional<NoteListItem> NoteCatalog::createNote(const std::string& title,
                                                    const std::filesystem::path& folder,
                                                    std::string_view body) {
  if(!library_) return std::nullopt;
  NoteMetadata metadata;
  metadata.id = generateNoteId();
  metadata.title = uniqueTitle(title, folder);
  auto path = library_->createNote(metadata, body);
  // The full walk rather than one file: creating in a folder may have created
  // the folder, and the sidebar tree is drawn from the directories the walk
  // reports -- including the empty ones, which no list of notes can name.
  if(!folder.empty()) path = library_->moveNote(path, folder);
  refresh();
  return NoteListItem {metadata.id,   path,          metadata.title,
                       metadata.tags, metadata.icon,
                       path.lexically_relative(library_->root()).parent_path()};
}

std::string NoteCatalog::preserveExternalVersion(const std::filesystem::path& path) {
  if(!library_) return {};
  const auto name = library_->preserveExternalVersion(path);
  // The rescued version is a note now, so it has to be indexed as one -- a note
  // the user cannot find in the sidebar is a note they will not find at all,
  // whatever the status line said. Read rather than handed over: those are
  // somebody else's bytes, not ours.
  if(!name.empty()) refreshFile(path.parent_path() / name);
  return name;
}

void NoteCatalog::deleteNote(const std::filesystem::path& path) {
  if(!library_) return;
  library_->deleteNote(path);
  // The file is gone rather than changed, which `refreshFile` handles: it drops
  // the rows a path no longer backs.
  refreshFile(path);
}

std::filesystem::path NoteCatalog::moveNote(const std::filesystem::path& path,
                                            const std::filesystem::path& folder) {
  if(!library_) return {};
  const auto target = library_->moveNote(path, folder);
  refresh();
  return target;
}

std::filesystem::path NoteCatalog::createFolder(const std::filesystem::path& folder) {
  if(!library_) return {};
  const auto target = library_->createFolder(folder);
  refresh();
  return target;
}

std::filesystem::path NoteCatalog::renameFolder(const std::filesystem::path& folder,
                                                const std::filesystem::path& newFolder) {
  if(!library_) return {};
  const auto target = library_->renameFolder(folder, newFolder);
  refresh();
  return target;
}

void NoteCatalog::deleteFolder(const std::filesystem::path& folder) {
  if(!library_) return;
  library_->deleteFolder(folder);
  refresh();
}

bool NoteCatalog::restoreFromTrash(const std::string& name) {
  if(!library_ || !library_->restoreFromTrash(name)) return false;
  return refresh();
}

bool NoteCatalog::refresh() {
  if(!library_) return false;
  ++revision_;
  const bool ok = index_.refreshChangedFiles();
  // Emplaced after the refresh, for the reason `open` gives: the note list
  // reads the index, so the index has to have seen the change first.
  organization_.emplace(*library_, index_);
  return ok;
}

bool NoteCatalog::refreshFile(const std::filesystem::path& path) {
  if(!library_) return false;
  return applied(index_.refreshFile(path));
}

bool NoteCatalog::applied(const LibraryIndex::FileRefresh& refresh) {
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
  if(refresh.listFieldsChanged) invalidateNoteList();
  return refresh.listFieldsChanged;
}

void NoteCatalog::invalidateNoteList() {
  if(!library_) return;
  organization_.emplace(*library_, index_);
}

}
