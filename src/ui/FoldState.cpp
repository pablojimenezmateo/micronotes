#include "CoreAliases.h"
#include "ui/FoldState.h"

#include "core/platform/DurableFile.h"

#include <fstream>
#include <sstream>

namespace micronotes::ui {

bool FoldState::folded(std::string_view noteId, std::string_view key) const {
  const auto note = notes_.find(noteId);
  if(note == notes_.end()) return false;
  return note->second.find(key) != note->second.end();
}

bool FoldState::anyFolded(std::string_view noteId) const {
  return notes_.find(noteId) != notes_.end();
}

bool FoldState::toggle(std::string_view noteId, std::string_view key) {
  auto& keys = notes_[std::string(noteId)];
  const auto found = keys.find(key);
  dirty_ = true;
  if(found != keys.end()) {
    keys.erase(found);
    if(keys.empty()) notes_.erase(std::string(noteId));
    return false;
  }
  keys.insert(std::string(key));
  return true;
}

void FoldState::unfold(std::string_view noteId, std::string_view key) {
  const auto note = notes_.find(noteId);
  if(note == notes_.end()) return;
  const auto found = note->second.find(key);
  if(found == note->second.end()) return;
  note->second.erase(found);
  dirty_ = true;
  if(note->second.empty()) notes_.erase(note);
}

void FoldState::clearNote(std::string_view noteId) {
  const auto note = notes_.find(noteId);
  if(note == notes_.end()) return;
  notes_.erase(note);
  dirty_ = true;
}

bool FoldState::empty() const {
  return notes_.empty();
}

bool FoldState::dirty() const {
  return dirty_;
}

bool FoldState::load(const std::filesystem::path& path) {
  notes_.clear();
  dirty_ = false;
  std::ifstream in(path);
  if(!in) return false;
  std::string line;
  while(std::getline(in, line)) {
    // "<note id>\t<fold key>". Keys are whitespace-collapsed when they are
    // built, so the tab is unambiguous.
    const auto tab = line.find('\t');
    if(tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
    notes_[line.substr(0, tab)].insert(line.substr(tab + 1));
  }
  return true;
}

bool FoldState::save(const std::filesystem::path& path) {
  std::ostringstream out;
  for(const auto& [noteId, keys] : notes_) {
    for(const auto& key : keys) out << noteId << '\t' << key << '\n';
  }
  dirty_ = false;
  return platform::writeFileDurably(path, out.str());
}

}
