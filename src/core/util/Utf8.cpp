#include "core/util/Utf8.h"

#include <algorithm>

namespace microcore::util {

std::size_t previousBoundary(std::string_view text, std::size_t offset) {
  offset = std::min(offset, text.size());
  if(offset == 0) return 0;
  --offset;
  // Walk back over continuation bytes to the lead byte. Bounded by the scan
  // reaching 0, so a malformed sequence cannot run off the start.
  while(offset > 0 && isContinuationByte(text[offset])) --offset;
  return offset;
}

std::size_t nextBoundary(std::string_view text, std::size_t offset) {
  if(offset >= text.size()) return text.size();
  ++offset;
  while(offset < text.size() && isContinuationByte(text[offset])) ++offset;
  return offset;
}

std::size_t countCodePoints(std::string_view text) {
  std::size_t count = 0;
  for(const char c : text) {
    if(!isContinuationByte(c)) ++count;
  }
  return count;
}

std::size_t offsetForCodePoint(std::string_view text, std::size_t index) {
  std::size_t offset = 0;
  while(index > 0 && offset < text.size()) {
    offset = nextBoundary(text, offset);
    --index;
  }
  return offset;
}

}
