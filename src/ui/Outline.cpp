#include "ui/Outline.h"

#include "doc/BlockScan.h"

#include <algorithm>
#include <limits>

namespace micronotes::ui {

std::vector<OutlineEntry> outlineOf(std::string_view source) {
  std::vector<OutlineEntry> entries;
  // scanBlocks already knows the difference between a heading and a line that
  // starts with a hash inside a fence, so the outline asks it rather than
  // scanning for "#" itself and getting that wrong in a second place.
  for(const auto& block : doc::scanBlocks(source)) {
    if(block.kind != doc::BlockKind::Heading) continue;
    OutlineEntry entry;
    entry.level = std::clamp(block.level, 1, 6);
    entry.offset = block.contentStart;
    entry.text = std::string(source.substr(block.contentStart, block.contentEnd - block.contentStart));
    // A closing run of hashes is decoration in ATX headings and is not part of
    // the title.
    while(!entry.text.empty() && (entry.text.back() == '#' || entry.text.back() == ' ')) {
      entry.text.pop_back();
    }
    if(entry.text.empty()) entry.text = "Untitled section";
    entries.push_back(std::move(entry));
  }

  // Indentation follows the shape of the note. A run of h3s under nothing is a
  // flat list; an h3 under an h2 is one step in. The stack holds the levels
  // currently open, so a heading that closes several at once steps back out.
  std::vector<int> open;
  for(auto& entry : entries) {
    while(!open.empty() && open.back() >= entry.level) open.pop_back();
    entry.depth = static_cast<int>(open.size());
    open.push_back(entry.level);
  }
  return entries;
}

std::size_t outlineEntryAt(const std::vector<OutlineEntry>& entries, std::size_t caret) {
  std::size_t found = std::numeric_limits<std::size_t>::max();
  for(std::size_t i = 0; i < entries.size(); ++i) {
    if(entries[i].offset > caret) break;
    found = i;
  }
  return found;
}

}
