#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace micronotes::ui {

// Which toggles a note has collapsed. Kept beside the library rather than
// inside the note: a fold is a view preference, and the `.md` file must not
// change because someone collapsed a heading.
class FoldState {
public:
  bool folded(std::string_view noteId, std::string_view key) const;
  // Whether a note has any fold at all, so a note with none costs no lookups.
  bool anyFolded(std::string_view noteId) const;
  // Returns the new state.
  bool toggle(std::string_view noteId, std::string_view key);
  void unfold(std::string_view noteId, std::string_view key);
  void clearNote(std::string_view noteId);
  bool empty() const;
  bool dirty() const;

  bool load(const std::filesystem::path& path);
  bool save(const std::filesystem::path& path);

private:
  // Transparent comparators throughout: every lookup arrives as a view.
  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> notes_;
  bool dirty_ = false;
};

}
