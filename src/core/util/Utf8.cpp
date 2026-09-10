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

std::size_t boundaryAtOrBefore(std::string_view text, std::size_t offset) {
  if(offset >= text.size()) return text.size();
  while(offset > 0 && isContinuationByte(text[offset])) --offset;
  return offset;
}

std::size_t nextBoundary(std::string_view text, std::size_t offset) {
  if(offset >= text.size()) return text.size();
  ++offset;
  while(offset < text.size() && isContinuationByte(text[offset])) ++offset;
  return offset;
}

CodePoint decodeAt(std::string_view text, std::size_t offset) {
  if(offset >= text.size()) return {0, text.size()};
  const auto lead = static_cast<unsigned char>(text[offset]);
  // How many bytes the lead byte claims, and how much of it is payload.
  std::size_t length = 1;
  char32_t value = lead;
  if(lead >= 0xF0) {
    length = 4;
    value = lead & 0x07u;
  } else if(lead >= 0xE0) {
    length = 3;
    value = lead & 0x0Fu;
  } else if(lead >= 0xC0) {
    length = 2;
    value = lead & 0x1Fu;
  } else if(lead >= 0x80) {
    // A continuation byte where a lead byte should be: not a character.
    return {0xFFFD, offset + 1};
  }
  if(offset + length > text.size()) return {0xFFFD, offset + 1};
  for(std::size_t i = 1; i < length; ++i) {
    if(!isContinuationByte(text[offset + i])) return {0xFFFD, offset + 1};
    value = (value << 6) | (static_cast<unsigned char>(text[offset + i]) & 0x3Fu);
  }
  // Surrogates and out-of-range values are ill-formed however they were
  // encoded, and a font asked for one would answer with whatever glyph that
  // number happens to name.
  if(value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return {0xFFFD, offset + length};
  return {value, offset + length};
}

std::size_t countCodePoints(std::string_view text) {
  std::size_t count = 0;
  for(const char c : text) {
    if(!isContinuationByte(c)) ++count;
  }
  return count;
}

}
