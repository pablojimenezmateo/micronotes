#include "doc/EditContext.h"

#include <algorithm>
#include <utility>

namespace micronotes::doc {

void BlockList::reset(std::string_view source, BlockSpan lent) {
  lent_ = lent;
  if(lent_.empty()) scanBlocksInto(source, &owned_);
  lastLine_ = !source.empty() && source.back() == '\n';
  if(lastLine_) {
    trailing_ = SourceBlock {};
    trailing_.kind = BlockKind::Blank;
    trailing_.start = source.size();
    trailing_.length = 0;
  }
}

std::size_t BlockList::indexAt(std::string_view source, std::size_t offset) const {
  const BlockSpan blocks = real();
  offset = std::min(offset, source.size());
  if(lastLine_ && offset == source.size()) return blocks.size();
  return blockIndexAt(blocks, offset);
}

Context contextAt(std::string_view source, std::size_t caret, BlockSpan lent) {
  Context context;
  context.blocks.reset(source, lent);
  context.index = context.blocks.indexAt(source, caret);
  return context;
}

Range rangeAt(std::string_view source, std::size_t from, std::size_t to, BlockSpan lent) {
  Range range;
  range.blocks.reset(source, lent);
  from = std::min(from, source.size());
  to = std::min(to, source.size());
  if(from > to) std::swap(from, to);
  range.first = range.blocks.indexAt(source, from);
  range.last = range.blocks.indexAt(source, to);
  if(range.last < range.first) std::swap(range.first, range.last);
  range.start = range.blocks[range.first].start;
  range.end = range.blocks[range.last].end();
  range.valid = range.end > range.start;
  return range;
}

Separator separatorFor(const BlockList& blocks, std::size_t first, std::size_t last) {
  Separator separator;
  separator.start = separator.end = blocks[last].end();
  const auto blankRun = [&blocks](std::size_t i) {
    // The synthetic last-line block has no width, so it separates nothing.
    return blocks[i].kind == BlockKind::Blank && blocks[i].end() > blocks[i].start;
  };
  std::size_t after = last;
  while(after + 1 < blocks.size() && blankRun(after + 1)) ++after;
  if(after > last) {
    separator.start = blocks[last].end();
    separator.end = blocks[after].end();
    return separator;
  }
  std::size_t before = first;
  while(before > 0 && blankRun(before - 1)) --before;
  if(before < first) {
    separator.start = blocks[before].start;
    separator.end = blocks[first].start;
    separator.leading = true;
  }
  return separator;
}

std::size_t leadingWhitespace(std::string_view source, const SourceBlock& block) {
  std::size_t i = block.start;
  while(i < block.end() && (source[i] == ' ' || source[i] == '\t')) ++i;
  return i - block.start;
}

std::size_t lineEndFrom(std::string_view source, std::size_t start) {
  const auto newline = source.find('\n', start);
  return newline == std::string_view::npos ? source.size() : newline;
}

std::size_t shiftedCaret(std::size_t caret, std::size_t contentStart, std::size_t newContentStart) {
  if(caret < contentStart) return newContentStart;
  return newContentStart + (caret - contentStart);
}

}
