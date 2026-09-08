#include "app/NoteCaches.h"

#include "core/perf/PerformanceCounters.h"
#include "ui/AppState.h"

#include <algorithm>
#include <utility>

namespace micronotes::app {

// Room for the parses an edit is part-way through replacing, so a keystroke
// inside a table does not sweep the note on every character. The layout's block
// cache keeps 256 for the same reason; a parse is heavier than a block layout,
// so this keeps fewer.
constexpr std::size_t kComplexSpare = 32;

const markdown::Document* ComplexParseCache::find(std::string_view source) {
  const auto found = entries_.find(source);
  if(found == entries_.end()) return nullptr;
  perf::addCounter(perf::CounterId::ComplexParsesReused);
  return &found->second;
}

const markdown::Document& ComplexParseCache::keep(std::string_view source, markdown::Document document) {
  return entries_.emplace(std::string(source), std::move(document)).first->second;
}

bool ComplexParseCache::sweepDue() const {
  return entries_.size() > live_ + kComplexSpare;
}

void ComplexParseCache::sweep(std::vector<std::string_view> live) {
  perf::addCounter(perf::CounterId::ComplexCacheSweeps);
  std::sort(live.begin(), live.end());
  live.erase(std::unique(live.begin(), live.end()), live.end());
  for(auto it = entries_.begin(); it != entries_.end();) {
    if(std::binary_search(live.begin(), live.end(), std::string_view(it->first))) {
      ++it;
    } else {
      perf::addCounter(perf::CounterId::ComplexParsesEvicted);
      it = entries_.erase(it);
    }
  }
  live_ = live.size();
}

void ImagePathCache::retarget(const std::filesystem::path& root) {
  if(root_ == root) return;
  entries_.clear();
  root_ = root;
}

const std::filesystem::path* ImagePathCache::find(std::string_view target) {
  const auto found = entries_.find(target);
  if(found == entries_.end()) return nullptr;
  perf::addCounter(perf::CounterId::ImagePathsReused);
  return &found->second;
}

const std::filesystem::path& ImagePathCache::keep(std::string_view target, std::filesystem::path path) {
  perf::addCounter(perf::CounterId::ImagePathsResolved);
  return entries_.emplace(std::string(target), std::move(path)).first->second;
}

const std::vector<library::NoteListItem>& WikiTargets::all(const ui::AppState& state) {
  if(!valid_) {
    notes_ = state.allNotes();
    valid_ = true;
  }
  return notes_;
}

void WikiTargets::invalidate() {
  valid_ = false;
  ++revision_;
  // Not cleared: the next `all()` copy-assigns over the standing vector, which
  // reuses its buffer and every note's string buffer with it. Clearing first
  // would give both back and buy nothing -- the list is rebuilt within the
  // frame in every case that invalidates it.
}

}
