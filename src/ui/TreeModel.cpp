#include "ui/TreeModel.h"

#include <algorithm>
#include <map>

namespace micronotes::ui {
namespace {

static std::string key(const std::filesystem::path& folder) {
  return folder.generic_string();
}

// A library root written with a trailing slash has an empty filename, so fall
// back to the directory above it before giving up on a name entirely.
static std::string rootName(const std::filesystem::path& root) {
  auto name = root.filename().generic_string();
  if(name.empty()) name = root.parent_path().filename().generic_string();
  return name.empty() ? "Library" : name;
}

}

bool TreeModel::expanded(const std::filesystem::path& folder) const {
  if(folder.empty()) return rootExpanded_;
  return expanded_.contains(key(folder));
}

void TreeModel::setExpanded(const std::filesystem::path& folder, bool value) {
  if(expanded(folder) == value) return;
  dirty_ = true;
  if(folder.empty()) {
    rootExpanded_ = value;
    return;
  }
  if(value) expanded_.insert(key(folder));
  else expanded_.erase(key(folder));
}

bool TreeModel::toggle(const std::filesystem::path& folder) {
  const bool value = !expanded(folder);
  setExpanded(folder, value);
  return value;
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
                                     const std::filesystem::path& root) const {
  // Children by parent, so the walk below is a lookup rather than a scan of
  // every folder at every level.
  std::map<std::string, std::vector<const library::FolderNode*>> children;
  std::map<std::string, std::vector<const library::NoteListItem*>> owned;
  const library::FolderNode* rootNode = nullptr;
  for(const auto& folder : folders) {
    if(folder.path.empty()) {
      rootNode = &folder;
      continue;
    }
    children[key(folder.path.parent_path())].push_back(&folder);
  }
  for(const auto& note : notes) {
    owned[key(note.path.lexically_relative(root).parent_path())].push_back(&note);
  }

  std::vector<TreeRow> rows;
  // Recursion by hand: the depth and the "is this open" question both belong to
  // the walk, and an explicit stack would say the same thing less clearly.
  const auto walk = [&](auto&& self, const std::filesystem::path& folder, int noteCount, int depth) -> void {
    TreeRow row;
    row.kind = TreeRowKind::Folder;
    row.depth = depth;
    row.folder = folder;
    row.label = folder.empty() ? rootName(root) : folder.filename().generic_string();
    row.noteCount = noteCount;
    auto& subfolders = children[key(folder)];
    auto& subnotes = owned[key(folder)];
    row.expandable = !subfolders.empty() || !subnotes.empty();
    row.expanded = expanded(folder);
    rows.push_back(std::move(row));
    if(!expanded(folder)) return;

    std::sort(subfolders.begin(), subfolders.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->path < rhs->path; });
    for(const auto* child : subfolders) self(self, child->path, child->noteCount, depth + 1);
    // Folders first, then notes: a folder that scrolls away under its own
    // notes is the thing every file tree gets wrong.
    std::sort(subnotes.begin(), subnotes.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->title < rhs->title; });
    for(const auto* note : subnotes) {
      TreeRow noteRow;
      noteRow.kind = TreeRowKind::Note;
      noteRow.depth = depth + 1;
      noteRow.folder = folder;
      noteRow.noteId = note->id;
      noteRow.label = note->title;
      noteRow.icon = note->icon;
      rows.push_back(std::move(noteRow));
    }
  };
  walk(walk, {}, rootNode ? rootNode->noteCount : 0, 0);
  return rows;
}

std::string TreeModel::serialize() const {
  std::string out = rootExpanded_ ? "" : "!root\n";
  for(const auto& folder : expanded_) {
    out += folder;
    out.push_back('\n');
  }
  return out;
}

void TreeModel::load(std::string_view value) {
  expanded_.clear();
  rootExpanded_ = true;
  std::size_t from = 0;
  while(from < value.size()) {
    const auto end = value.find('\n', from);
    const auto line = value.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from);
    if(line == "!root") rootExpanded_ = false;
    else if(!line.empty()) expanded_.insert(std::string(line));
    if(end == std::string_view::npos) break;
    from = end + 1;
  }
  dirty_ = false;
}

bool TreeModel::dirty() const {
  return dirty_;
}

}
