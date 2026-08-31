#include "doc/Fold.h"

#include <cctype>

namespace micronotes::doc {
namespace {

bool blank(const SourceBlock& block) {
  return block.kind == BlockKind::Blank;
}

}

bool foldableKind(BlockKind kind) {
  return kind == BlockKind::Heading || isListKind(kind);
}

std::size_t foldEnd(const std::vector<SourceBlock>& blocks, std::size_t index) {
  if(index >= blocks.size()) return blocks.size();
  const SourceBlock& head = blocks[index];
  std::size_t end = index + 1;

  if(head.kind == BlockKind::Heading) {
    std::size_t scan = index + 1;
    while(scan < blocks.size()) {
      const SourceBlock& block = blocks[scan];
      // A heading of the same or higher rank starts its own section.
      if(block.kind == BlockKind::Heading && block.level <= head.level) break;
      ++scan;
      if(!blank(block)) end = scan;
    }
    return end;
  }

  if(isListKind(head.kind)) {
    std::size_t scan = index + 1;
    while(scan < blocks.size()) {
      const SourceBlock& block = blocks[scan];
      // Blank lines inside a list belong to it only if a deeper item follows.
      if(blank(block)) { ++scan; continue; }
      if(!isListKind(block.kind) || block.listDepth <= head.listDepth) break;
      ++scan;
      end = scan;
    }
    return end;
  }

  return end;
}

bool foldable(const std::vector<SourceBlock>& blocks, std::size_t index) {
  if(index >= blocks.size() || !foldableKind(blocks[index].kind)) return false;
  return foldEnd(blocks, index) > index + 1;
}

std::string foldKey(std::string_view source, const SourceBlock& block) {
  std::string key;
  switch(block.kind) {
    case BlockKind::Heading: key = "h" + std::to_string(block.level); break;
    case BlockKind::Bullet:
    case BlockKind::Ordered:
    case BlockKind::Todo: key = "li" + std::to_string(block.listDepth); break;
    default: key = "b"; break;
  }
  key.push_back(':');
  const std::size_t prefix = key.size();

  // Whitespace-collapsed so the key can never carry the separators the fold
  // file is written with.
  const std::size_t from = std::min(block.contentStart, source.size());
  const std::size_t to = std::min(block.contentEnd, source.size());
  bool pendingSpace = false;
  for(std::size_t i = from; i < to && key.size() < 140; ++i) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if(c == '\n' || c == '\t' || c == '\r' || c == ' ') {
      pendingSpace = key.size() > prefix;
      continue;
    }
    if(pendingSpace) {
      key.push_back(' ');
      pendingSpace = false;
    }
    key.push_back(source[i]);
  }
  return key;
}

}
