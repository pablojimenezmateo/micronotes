#pragma once

#include "library/Organization.h"

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

enum class TreeRowKind {
  Folder,
  Note
};

// One line of the sidebar tree, already flattened for drawing. A row knows its
// depth rather than its parent: the sidebar draws a list, and the nesting is
// entirely an indent.
struct TreeRow {
  TreeRowKind kind = TreeRowKind::Folder;
  int depth = 0;
  // A folder row's own path; a note row's parent folder. Empty is the library
  // root, which is a real row so that selecting and dropping onto it work the
  // same way as for any other folder.
  std::filesystem::path folder;
  std::string noteId;   // note rows only
  std::string label;
  std::string icon;     // the note's `icon:`, empty when it has none
  int noteCount = 0;    // folder rows: notes directly inside
  bool expandable = false;
  bool expanded = false;
};

// Which folders the sidebar has open. Kept apart from the library itself: a
// disclosure triangle is a view preference, so opening one must not touch a
// single file on disk.
class TreeModel {
public:
  bool expanded(const std::filesystem::path& folder) const;
  void setExpanded(const std::filesystem::path& folder, bool value);
  bool toggle(const std::filesystem::path& folder);
  // Opens every ancestor of `folder`, so revealing a note can never leave it
  // hidden behind a parent someone collapsed earlier.
  void reveal(const std::filesystem::path& folder);

  // The rows to draw, top to bottom. Rebuilt from the library rather than
  // cached: a library is a few hundred entries, and a stale tree is a worse
  // problem than a rebuilt one.
  // `root` is the library directory: note paths are absolute, and the tree
  // speaks in paths relative to it. The root row is labelled with its name.
  std::vector<TreeRow> rows(const std::vector<library::FolderNode>& folders,
                            const std::vector<library::NoteListItem>& notes,
                            const std::filesystem::path& root) const;

  // One expanded folder path per line, so the file stays readable and a path
  // containing any character but a newline round-trips unescaped.
  std::string serialize() const;
  void load(std::string_view value);
  bool dirty() const;

private:
  // Transparent comparator: every lookup arrives as a path, not a string.
  std::set<std::string, std::less<>> expanded_;
  // The root has no path to key on, and it starts open: a sidebar that opens
  // empty reads as broken rather than tidy.
  bool rootExpanded_ = true;
  bool dirty_ = false;
};

}
