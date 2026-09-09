#include "ui/TreeModel.h"

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"

#include <algorithm>
#include <map>

namespace micronotes::ui {
namespace {

static std::string key(const std::filesystem::path& folder) {
  return folder.generic_string();
}

// A library root written with a trailing slash has an empty filename, so fall
// back to the directory above it before giving up on a name entirely.
}

bool TreeModel::expanded(const std::filesystem::path& folder) const {
  // The library root is always open, because it has no row to close it with:
  // the tree starts at the root's *contents*, and hiding the whole tree is what
  // the sidebar's Notebooks band does. It used to carry a `rootExpanded_` flag
  // for a chevron that no longer exists.
  //
  // Answered rather than refused so `reveal` can keep walking from the root
  // without a special case at the top of every loop.
  if(folder.empty()) return true;
  return expanded_.contains(key(folder));
}

void TreeModel::setExpanded(const std::filesystem::path& folder, bool value) {
  // Nothing to set for the root: see `expanded`.
  if(folder.empty()) return;
  if(expanded(folder) == value) return;
  dirty_ = true;
  ++revision_;
  if(value) expanded_.insert(key(folder));
  else expanded_.erase(key(folder));
}

bool TreeModel::toggle(const std::filesystem::path& folder) {
  // Reports what the folder *is* afterwards, not what was asked for. Those
  // differ for the library root, which is permanently open and ignores the
  // request -- and reporting "closed" for something that is open is the kind of
  // answer a caller would act on.
  setExpanded(folder, !expanded(folder));
  return expanded(folder);
}

void TreeModel::reveal(const std::filesystem::path& folder) {
  setExpanded({}, true);
  std::filesystem::path walk;
  for(const auto& part : folder) {
    walk /= part;
    setExpanded(walk, true);
  }
}

std::vector<TreeRow> TreeModel::rows(const std::vector<library::FolderNode>& folders,
                                     const std::vector<library::NoteListItem>& notes,
                                     const std::vector<library::CompanionEntry>& companions) const {
  // Children by parent, so the walk below is a lookup rather than a scan of
  // every folder at every level.
  std::map<std::string, std::vector<const library::FolderNode*>> children;
  std::map<std::string, std::vector<const library::NoteListItem*>> owned;
  // The `files/` directory a notebook holds, by the notebook; and what sits
  // directly inside each companion directory, by that directory.
  std::map<std::string, const library::CompanionEntry*> filesDirOf;
  std::map<std::string, std::vector<const library::CompanionEntry*>> companionChildren;
  for(const auto& folder : folders) {
    // The root's own entry is not a child of anything, and has no row.
    if(folder.path.empty()) continue;
    children[key(folder.path.parent_path())].push_back(&folder);
  }
  for(const auto& note : notes) {
    owned[key(note.folder)].push_back(&note);
  }
  for(const auto& entry : companions) {
    if(entry.directory && library::isFilesDir(entry.path)) filesDirOf[key(entry.folder)] = &entry;
    else companionChildren[key(entry.folder)].push_back(&entry);
  }

  std::vector<TreeRow> rows;
  // A companion directory and everything under it. Folders first, then files,
  // each by name -- the same rule the notebooks follow, for the same reason.
  const auto emitCompanionDir = [&](auto&& self, const library::CompanionEntry& dir, int depth) -> void {
    auto& inside = companionChildren[key(dir.path)];
    TreeRow row;
    row.kind = TreeRowKind::FilesFolder;
    row.depth = depth;
    row.folder = dir.folder;
    row.file = dir.path;
    row.label = dir.path.filename().generic_string();
    row.noteCount = static_cast<int>(inside.size());
    row.expandable = !inside.empty();
    row.expanded = expanded(dir.path);
    rows.push_back(std::move(row));
    if(!expanded(dir.path)) return;
    std::sort(inside.begin(), inside.end(), [](const auto* lhs, const auto* rhs) {
      if(lhs->directory != rhs->directory) return lhs->directory;
      return lhs->path.filename() < rhs->path.filename();
    });
    for(const auto* child : inside) {
      if(child->directory) {
        self(self, *child, depth + 1);
        continue;
      }
      TreeRow fileRow;
      fileRow.kind = TreeRowKind::File;
      fileRow.depth = depth + 1;
      fileRow.folder = child->folder;
      fileRow.file = child->path;
      fileRow.label = child->path.filename().generic_string();
      rows.push_back(std::move(fileRow));
    }
  };

  // What is *inside* `folder`, at `depth`. Recursion by hand: the depth and the
  // "is this open" question both belong to the walk, and an explicit stack
  // would say the same thing less clearly.
  //
  // The contents rather than the folder itself, which is what leaves the
  // library root without a row. It had one, labelled with the library
  // directory's own name, and it was a container inside a container: the
  // sidebar's Notebooks band already names the section and already collapses
  // it, so the root row said the same thing a second time and indented every
  // other row one step to do it.
  const auto emit = [&](auto&& self, const std::filesystem::path& folder, int depth) -> void {
    auto& subfolders = children[key(folder)];
    auto& subnotes = owned[key(folder)];
    std::sort(subfolders.begin(), subfolders.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->path < rhs->path; });
    // Folders first, then notes: a folder that scrolls away under its own
    // notes is the thing every file tree gets wrong.
    for(const auto* child : subfolders) {
      TreeRow row;
      row.kind = TreeRowKind::Folder;
      row.depth = depth;
      row.folder = child->path;
      row.label = child->path.filename().generic_string();
      row.noteCount = child->noteCount;
      row.expandable = !children[key(child->path)].empty() || !owned[key(child->path)].empty() ||
                       filesDirOf.contains(key(child->path));
      row.expanded = expanded(child->path);
      rows.push_back(std::move(row));
      if(expanded(child->path)) self(self, child->path, depth + 1);
    }
    // The notebook's files between its notebooks and its notes: a container,
    // so above the leaves, and always in the same place whatever it is called
    // relative to the notebooks beside it.
    if(const auto found = filesDirOf.find(key(folder)); found != filesDirOf.end()) {
      emitCompanionDir(emitCompanionDir, *found->second, depth);
    }
    std::sort(subnotes.begin(), subnotes.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->title < rhs->title; });
    for(const auto* note : subnotes) {
      TreeRow noteRow;
      noteRow.kind = TreeRowKind::Note;
      noteRow.depth = depth;
      noteRow.folder = folder;
      noteRow.noteId = note->id;
      noteRow.label = note->title;
      noteRow.icon = note->icon;
      rows.push_back(std::move(noteRow));
    }
  };
  emit(emit, {}, 0);
  perf::addCounter(perf::CounterId::TreeRowsBuilt, rows.size());
  return rows;
}

std::string TreeModel::serialize() const {
  std::string out;
  for(const auto& folder : expanded_) {
    out += folder;
    out.push_back('\n');
  }
  return out;
}

void TreeModel::load(std::string_view value) {
  expanded_.clear();
  std::size_t from = 0;
  while(from < value.size()) {
    const auto end = value.find('\n', from);
    const auto line = value.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from);
    // `!root` is a file written when the library root had a row of its own and
    // could be closed. It has neither now, so the line is read and dropped
    // rather than taken for a folder called "!root".
    if(line == "!root") { /* an older file's collapsed root */ }
    else if(!line.empty()) expanded_.insert(std::string(line));
    if(end == std::string_view::npos) break;
    from = end + 1;
  }
  dirty_ = false;
  ++revision_;
}

bool TreeModel::dirty() const {
  return dirty_;
}

}
