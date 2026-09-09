#include "library/Organization.h"

#include "core/util/StringUtil.h"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace micronotes::library {

OrganizationService::OrganizationService(const Library& library, const LibraryIndex& index)
    : library_(library), index_(index) {}

const std::vector<NoteListItem>& OrganizationService::notes() const {
  if(notes_) return *notes_;
  std::vector<NoteListItem> notes;
  // From the index, which has already read every note. The refresh that runs
  // before this opens each changed file once and writes its id, path, title,
  // tags and icon to SQLite -- which is the whole of what a note list needs --
  // so the list is one statement. It used to be a second recursive walk of the
  // library and a re-open plus a front-matter parse of every note in it, a few
  // microseconds after the refresh had read the same files for the same fields.
  auto indexed = index_.notes();
  if(!indexed.empty()) {
    notes.reserve(indexed.size());
    for(auto& row : indexed) {
      // `relativePath` is what the index stores, so the absolute path is a join
      // and the folder is its parent -- neither needs `lexically_relative`.
      auto folder = row.relativePath.parent_path();
      notes.push_back({
        std::move(row.id),
        library_.root() / row.relativePath,
        std::move(row.title),
        std::move(row.tags),
        std::move(row.icon),
        std::move(folder),
      });
    }
    // The directories come from the refresh's own walk, so the startup is one
    // walk of the tree rather than two. The companions ride the same walk.
    directories_ = index_.directories();
    if(!companions_) setCompanions(index_.companions());
  } else {
    // No index -- it would not open, or it is genuinely empty. Either way the
    // notes have to come from the tree, and the walk that finds them reports
    // the directories alongside them.
    std::vector<std::filesystem::directory_entry> files;
    std::vector<CompanionEntry> companions;
    library_.walk(&files, &directories_, &companions);
    if(!companions_) setCompanions(std::move(companions));
    notes.reserve(files.size());
    for(const auto& entry : files) {
      const auto& path = entry.path();
      auto metadata = library_.loadNoteMetadata(path);
      // One `lexically_relative` per note, here, rather than one per note per
      // caller. Its `generic_string` form is also what the fallback id is made
      // from, so the two share the single call.
      auto relative = path.lexically_relative(library_.root());
      notes.push_back({
        metadata.id.empty() ? fallbackNoteId(relative.generic_string()) : metadata.id,
        path,
        metadata.title.empty() ? path.stem().string() : metadata.title,
        std::move(metadata.tags),
        std::move(metadata.icon),
        relative.parent_path(),
      });
    }
  }
  std::sort(notes.begin(), notes.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.title < rhs.title;
  });
  notes_ = std::move(notes);
  // Built here rather than on first use, so its keys -- views into the ids in
  // `notes_` -- cannot outlive or predate the vector they point into. Two notes
  // carrying the same front-matter id resolve to the last of them, which is
  // what the linear scan this replaces did as well.
  byId_.clear();
  byId_.reserve(notes_->size());
  for(std::size_t i = 0; i < notes_->size(); ++i) byId_[(*notes_)[i].id] = i;
  return *notes_;
}

const std::vector<FolderNode>& OrganizationService::folders() const {
  if(folders_) return *folders_;
  // Establishes `directories_` if the note list has not been built yet. Both
  // are memoised off the one scan, which is what makes reading the list below
  // safe rather than a second walk waiting to happen.
  notes();
  std::vector<FolderNode> folders;
  folders.reserve(directories_.size() + 1);
  for(const auto& relative : directories_) folders.push_back({relative, 0});
  // By folder rather than by scan. This was a `find_if` over every folder for
  // every note, which on a library filed into as many folders as it has notes
  // is quadratic in the library.
  std::unordered_map<std::string, std::size_t> at;
  at.reserve(folders.size() * 2);
  for(std::size_t i = 0; i < folders.size(); ++i) at.emplace(folders[i].path.generic_string(), i);
  for(const auto& note : notes()) {
    const auto key = note.folder.generic_string();
    const auto found = at.find(key);
    if(found == at.end()) {
      at.emplace(key, folders.size());
      folders.push_back({note.folder, 1});
    } else {
      ++folders[found->second].noteCount;
    }
  }
  std::sort(folders.begin(), folders.end(), [](const auto& lhs, const auto& rhs) { return lhs.path < rhs.path; });
  folders_ = std::move(folders);
  return *folders_;
}

const std::vector<std::string>& OrganizationService::tags() const {
  if(tags_) return *tags_;
  std::set<std::string> unique;
  for(const auto& note : notes()) {
    for(const auto& tag : note.tags) unique.insert(tag);
  }
  tags_ = std::vector<std::string> {unique.begin(), unique.end()};
  return *tags_;
}

void OrganizationService::setCompanions(std::vector<CompanionEntry> entries) const {
  // Sorted once, on the way in, so the tree and the search read a list in path
  // order rather than each sorting a copy.
  std::sort(entries.begin(), entries.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.path < rhs.path; });
  companions_ = std::move(entries);
}

const std::vector<CompanionEntry>& OrganizationService::companions() const {
  // Established by the same scan that fills the directories; see `notes()`.
  if(!companions_) notes();
  if(!companions_) companions_.emplace();
  return *companions_;
}

void OrganizationService::replaceCompanionsUnder(const std::filesystem::path& filesDir,
                                                 std::vector<CompanionEntry> entries) {
  auto list = companions();
  const auto under = [&](const CompanionEntry& entry) {
    return filesRootOf(entry.path) == filesDir;
  };
  list.erase(std::remove_if(list.begin(), list.end(), under), list.end());
  for(auto& entry : entries) list.push_back(std::move(entry));
  setCompanions(std::move(list));
}

std::vector<CompanionEntry> OrganizationService::companionsMatching(std::string_view query) const {
  std::vector<CompanionEntry> out;
  if(query.empty()) return out;
  std::string needle(query);
  util::toLowerAsciiInPlace(needle);
  // The same cap the index search applies, and for the same reason: nothing
  // downstream will draw more, and a one-letter query matches most of a
  // library.
  constexpr std::size_t kMaxResults = 200;
  for(const auto& entry : companions()) {
    if(entry.directory) continue;
    std::string name = entry.path.filename().string();
    util::toLowerAsciiInPlace(name);
    if(name.find(needle) == std::string::npos) continue;
    out.push_back(entry);
    if(out.size() >= kMaxResults) break;
  }
  return out;
}

// Both filters copy only what they return. Taking the note by value to test it
// deep-copied a path and three strings for every note in the library, including
// every note the filter was about to reject.
std::vector<NoteListItem> OrganizationService::notesInFolder(const std::filesystem::path& relativeFolder) const {
  std::vector<NoteListItem> out;
  for(const auto& note : notes()) {
    if(note.folder == relativeFolder) out.push_back(note);
  }
  return out;
}

std::vector<NoteListItem> OrganizationService::notesWithTag(const std::string& tag) const {
  std::vector<NoteListItem> out;
  for(const auto& note : notes()) {
    if(std::find(note.tags.begin(), note.tags.end(), tag) != note.tags.end()) out.push_back(note);
  }
  return out;
}

const NoteListItem* OrganizationService::noteById(std::string_view noteId) const {
  const auto& list = notes();
  const auto found = byId_.find(noteId);
  return found == byId_.end() ? nullptr : &list[found->second];
}

std::optional<NoteListItem> OrganizationService::findNote(std::string_view noteId) const {
  const NoteListItem* note = noteById(noteId);
  if(!note) return std::nullopt;
  return *note;
}

}
