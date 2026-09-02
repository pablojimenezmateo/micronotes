#pragma once

#include <cstdint>
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
  // Moves on every change to what is collapsed. The live surface hands it to
  // the layout, which would otherwise have to ask "is this folded?" once per
  // block on every frame -- and each of those asks builds a fold key -- to
  // establish that nothing had moved since the last one.
  std::uint64_t revision() const { return revision_; }

  bool load(const std::filesystem::path& path);
  bool save(const std::filesystem::path& path);

private:
  // Transparent comparators throughout: every lookup arrives as a view.
  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> notes_;
  bool dirty_ = false;
  std::uint64_t revision_ = 0;
};

}
