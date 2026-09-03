#include "library/Organization.h"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace micronotes::library {

OrganizationService::OrganizationService(const Library& library) : library_(library) {}

const std::vector<NoteListItem>& OrganizationService::notes() const {
  if(notes_) return *notes_;
  std::vector<NoteListItem> notes;
  for(const auto& path : library_.noteFiles()) {
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
  std::sort(notes.begin(), notes.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.title < rhs.title;
  });
  notes_ = std::move(notes);
  // Built here rather than on first use, so its keys -- views into the ids in
  // `notes_` -- cannot outlive or predate the vector they point into. Two notes
  // carrying the same front-matter id resolve to the last of them, which is
  // what the linear scan this replaces did as well.
  index_.clear();
  index_.reserve(notes_->size());
  for(std::size_t i = 0; i < notes_->size(); ++i) index_[(*notes_)[i].id] = i;
  return *notes_;
}

const std::vector<FolderNode>& OrganizationService::folders() const {
  if(folders_) return *folders_;
  std::vector<FolderNode> folders;
  if(std::filesystem::exists(library_.root())) {
    for(const auto& entry : std::filesystem::recursive_directory_iterator(library_.root())) {
      if(!entry.is_directory()) continue;
      const auto path = entry.path();
      if(path == library_.root()) continue;
      if(path.string().find((library_.root() / ".micronotes").string()) == 0) continue;
      folders.push_back({path.lexically_relative(library_.root()), 0});
    }
  }
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
  const auto found = index_.find(noteId);
  return found == index_.end() ? nullptr : &list[found->second];
}

std::optional<NoteListItem> OrganizationService::findNote(std::string_view noteId) const {
  const NoteListItem* note = noteById(noteId);
  if(!note) return std::nullopt;
  return *note;
}

}
