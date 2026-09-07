#pragma once

#include "library/Organization.h"

#include <cstdint>
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
  // A folder row's own path; a note row's parent folder. Empty means the
  // library root -- which a *note* row can name, since notes sit directly in
  // it, but which has no folder row of its own: the tree starts at the root's
  // contents. See `rows`.
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
  // Returns what the folder is open-or-closed *afterwards*, which for the
  // library root is always open: see `expanded`.
  bool toggle(const std::filesystem::path& folder);
  // Opens every ancestor of `folder`, so revealing a note can never leave it
  // hidden behind a parent someone collapsed earlier.
  void reveal(const std::filesystem::path& folder);

  // Bumped by every change to what is open. The sidebar memoises its row list
  // and this is half of the key: the other half is the library revision, and
  // between them they say whether the rows the last frame built still stand.
  std::uint64_t revision() const { return revision_; }

  // The rows to draw, top to bottom.
  //
  // The tree starts at the root's **contents**, with no row for the root
  // itself. It had one, labelled with the library directory's own name, and it
  // was a container inside a container: the sidebar's Notebooks band already
  // names the section and already collapses it, so the root row said the same
  // thing again and indented every other row one step to do it.
  //
  // This is O(library), not O(viewport): it relativises a path and builds a map
  // key for every note before it can place the first row. The caller is
  // expected to hold the result and ask again only when `revision()` moves.
  std::vector<TreeRow> rows(const std::vector<library::FolderNode>& folders,
                            const std::vector<library::NoteListItem>& notes) const;

  // One expanded folder path per line, so the file stays readable and a path
  // containing any character but a newline round-trips unescaped.
  std::string serialize() const;
  void load(std::string_view value);
  bool dirty() const;

private:
  // Transparent comparator: every lookup arrives as a path, not a string.
  //
  // Real folders only. The root used to need a `rootExpanded_` flag beside this
  // because it has no path to key on; it has no row to close either, now, so
  // there is nothing to remember about it.
  std::set<std::string, std::less<>> expanded_;
  bool dirty_ = false;
  std::uint64_t revision_ = 0;
};

}
