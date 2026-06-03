#include "library/Organization.h"

#include <algorithm>
#include <set>

namespace micronotes::library {

OrganizationService::OrganizationService(const Library& library) : library_(library) {}

const std::vector<NoteListItem>& OrganizationService::allNotes() const {
  if(notes_) return *notes_;
  std::vector<NoteListItem> notes;
  for(const auto& path : library_.noteFiles()) {
    auto metadata = library_.loadNoteMetadata(path);
    notes.push_back({
      metadata.id,
      path,
      metadata.title.empty() ? path.stem().string() : metadata.title,
      std::move(metadata.tags),
    });
  }
  std::sort(notes.begin(), notes.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.title < rhs.title;
  });
  notes_ = std::move(notes);
  return *notes_;
}

std::vector<FolderNode> OrganizationService::folders() const {
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
  for(const auto& note : allNotes()) {
    const auto parent = note.path.lexically_relative(library_.root()).parent_path();
    auto found = std::find_if(folders.begin(), folders.end(), [&](const auto& folder) { return folder.path == parent; });
    if(found == folders.end()) folders.push_back({parent, 1});
    else ++found->noteCount;
  }
  std::sort(folders.begin(), folders.end(), [](const auto& lhs, const auto& rhs) { return lhs.path < rhs.path; });
  folders_ = std::move(folders);
  return *folders_;
}

std::vector<std::string> OrganizationService::tags() const {
  if(tags_) return *tags_;
  std::set<std::string> unique;
  for(const auto& note : allNotes()) {
    for(const auto& tag : note.tags) unique.insert(tag);
  }
  tags_ = std::vector<std::string> {unique.begin(), unique.end()};
  return *tags_;
}

std::vector<NoteListItem> OrganizationService::notesInFolder(const std::filesystem::path& relativeFolder) const {
  std::vector<NoteListItem> out;
  for(auto note : allNotes()) {
    const auto relative = note.path.lexically_relative(library_.root());
    if(relative.parent_path() == relativeFolder) out.push_back(std::move(note));
  }
  return out;
}

std::vector<NoteListItem> OrganizationService::notesWithTag(const std::string& tag) const {
  std::vector<NoteListItem> out;
  for(auto note : allNotes()) {
    if(std::find(note.tags.begin(), note.tags.end(), tag) != note.tags.end()) {
      out.push_back(std::move(note));
    }
  }
  return out;
}

std::optional<NoteListItem> OrganizationService::findNote(std::string_view noteId) const {
  for(const auto& note : allNotes()) {
    if(note.id == noteId) return note;
  }
  return std::nullopt;
}

}
