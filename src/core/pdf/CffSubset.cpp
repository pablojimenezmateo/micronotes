#include "core/pdf/CffSubset.h"

#include "core/pdf/SfntRead.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace microcore::pdf {
namespace {

using sfnt::u16At;
using sfnt::u32At;
using sfnt::u8At;

// One CFF INDEX: where its entries are, and where the structure ends.
struct CffIndex {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
  std::size_t end = 0;
  bool ok = false;
};

// An offset of `size` bytes, big-endian, as a CFF INDEX writes one.
std::uint32_t offsetAt(std::string_view data, std::size_t at, std::uint8_t size) {
  std::uint32_t value = 0;
  for(std::uint8_t i = 0; i < size; ++i) value = (value << 8) | u8At(data, at + i);
  return value;
}

CffIndex readIndex(std::string_view cff, std::size_t at) {
  CffIndex index;
  if(at + 2 > cff.size()) return index;
  const std::uint16_t count = u16At(cff, at);
  if(count == 0) {
    index.end = at + 2;
    index.ok = true;
    return index;
  }
  const std::uint8_t offSize = u8At(cff, at + 2);
  if(offSize < 1 || offSize > 4) return index;
  const std::size_t offsets = at + 3;
  const std::size_t data = offsets + (static_cast<std::size_t>(count) + 1) * offSize - 1;
  index.entries.reserve(count);
  for(std::uint16_t i = 0; i < count; ++i) {
    const std::uint32_t from = offsetAt(cff, offsets + static_cast<std::size_t>(i) * offSize, offSize);
    const std::uint32_t to =
      offsetAt(cff, offsets + (static_cast<std::size_t>(i) + 1) * offSize, offSize);
    if(from < 1 || to < from) return index;
    if(data + to > cff.size()) return index;
    index.entries.emplace_back(static_cast<std::uint32_t>(data + from),
                               static_cast<std::uint32_t>(data + to));
  }
  index.end = data + offsetAt(cff, offsets + static_cast<std::size_t>(count) * offSize, offSize);
  index.ok = index.end <= cff.size();
  return index;
}

// The operand of a CFF DICT operator, or -1 when the DICT does not carry one.
//
// Only whole-number operands are read, and only the last one before the
// operator, which is what every offset in a Top DICT is. Reals and the
// multi-operand entries (`FontMatrix`, `ROS`) are stepped over rather than
// decoded: this is looking for one offset, not interpreting a font.
std::int64_t dictOperand(std::string_view dict, int wanted) {
  std::vector<std::int64_t> operands;
  std::size_t at = 0;
  while(at < dict.size()) {
    const std::uint8_t b0 = u8At(dict, at);
    if(b0 <= 21) {
      int op = b0;
      ++at;
      if(b0 == 12) {
        op = 1200 + u8At(dict, at);
        ++at;
      }
      if(op == wanted) return operands.empty() ? -1 : operands.back();
      operands.clear();
      continue;
    }
    if(b0 == 28) {
      operands.push_back(static_cast<std::int16_t>(u16At(dict, at + 1)));
      at += 3;
      continue;
    }
    if(b0 == 29) {
      operands.push_back(static_cast<std::int32_t>(u32At(dict, at + 1)));
      at += 5;
      continue;
    }
    if(b0 == 30) {
      // A real, nibble encoded and terminated by an `f` nibble.
      ++at;
      while(at < dict.size()) {
        const std::uint8_t byte = u8At(dict, at);
        ++at;
        if((byte & 0x0F) == 0x0F || (byte >> 4) == 0x0F) break;
      }
      operands.push_back(0);
      continue;
    }
    if(b0 >= 32 && b0 <= 246) {
      operands.push_back(static_cast<int>(b0) - 139);
      ++at;
      continue;
    }
    if(b0 >= 247 && b0 <= 250) {
      operands.push_back((static_cast<int>(b0) - 247) * 256 + u8At(dict, at + 1) + 108);
      at += 2;
      continue;
    }
    if(b0 >= 251 && b0 <= 254) {
      operands.push_back(-((static_cast<int>(b0) - 251) * 256) - u8At(dict, at + 1) - 108);
      at += 2;
      continue;
    }
    // A reserved byte. The DICT is not one this understands, and guessing past
    // it would find an offset that means something else.
    return -1;
  }
  return -1;
}

}

// The trick that makes this a filter rather than a re-layout: the charstring
// INDEX is rebuilt **inside the byte range it already occupied**. Its entry
// count and its offset size do not change, the kept charstrings are packed at
// the front of its data area, every dropped glyph becomes a one-byte
// `endchar` -- a glyph that draws nothing -- and the room that frees up is
// left where it is, zero filled.
//
// So the INDEX shrinks logically while the table does not move at all, and
// every absolute offset in the Top DICT -- to the charset, the encoding, the
// private DICT -- still points where it pointed. Rewriting those is what would
// turn this into a CFF writer, and the two things that made the file big are
// paid without one: the charstring data compresses away to nothing, and so
// does the offset array, which was 8 KB of ascending three-byte numbers and is
// now 8 KB of the same small number repeated.
//
// What is left in a subsetted CFF is the string INDEX (the glyph names, which
// a PDF reads by number and so never consults), the global and local
// subroutines, and the charset. Those are the next 30 KB and each needs
// something this deliberately does not do: interpreting charstrings to find
// which subroutines they call, or rewriting the offsets that reach them.
//
// Empty when the table is not a shape this understands, which leaves the
// caller embedding the original.
std::string subsetCffCharstrings(std::string_view cff, const std::set<std::uint16_t>& kept) {
  constexpr char kEndchar = 14;
  if(cff.size() < 4) return {};
  const std::uint8_t headerSize = u8At(cff, 2);
  const CffIndex names = readIndex(cff, headerSize);
  if(!names.ok) return {};
  const CffIndex topDicts = readIndex(cff, names.end);
  if(!topDicts.ok || topDicts.entries.empty()) return {};
  const auto& top = topDicts.entries.front();
  const std::int64_t charStringsAt =
    dictOperand(cff.substr(top.first, top.second - top.first), 17);
  if(charStringsAt <= 0 || static_cast<std::size_t>(charStringsAt) >= cff.size()) return {};
  const CffIndex charStrings = readIndex(cff, static_cast<std::size_t>(charStringsAt));
  if(!charStrings.ok || charStrings.entries.empty()) return {};

  const auto count = static_cast<std::uint16_t>(charStrings.entries.size());
  const std::uint8_t offSize = u8At(cff, static_cast<std::size_t>(charStringsAt) + 2);
  const std::size_t offsets = static_cast<std::size_t>(charStringsAt) + 3;
  // Offsets in an INDEX are one-based from the byte *before* the data, which
  // is what makes the first one 1 rather than 0.
  const std::size_t base = offsets + (static_cast<std::size_t>(count) + 1) * offSize - 1;
  const std::size_t room = charStrings.entries.back().second - (base + 1);

  std::string data;
  data.reserve(room);
  std::vector<std::uint32_t> newOffsets;
  newOffsets.reserve(static_cast<std::size_t>(count) + 1);
  for(std::uint16_t glyph = 0; glyph < count; ++glyph) {
    newOffsets.push_back(static_cast<std::uint32_t>(data.size()) + 1);
    const auto& range = charStrings.entries[glyph];
    if(kept.count(glyph) != 0 && range.second > range.first) {
      data.append(cff.substr(range.first, range.second - range.first));
      continue;
    }
    data.push_back(kEndchar);
  }
  newOffsets.push_back(static_cast<std::uint32_t>(data.size()) + 1);
  // A font whose glyphs are almost all empty could in principle need more room
  // than it had -- one `endchar` where there were zero bytes. Nothing here is
  // such a font, and the answer if one appears is to embed it whole rather
  // than to write past the table.
  if(data.size() > room) return {};

  std::string out(cff);
  for(std::size_t i = 0; i < newOffsets.size(); ++i) {
    std::uint32_t value = newOffsets[i];
    for(std::uint8_t byte = offSize; byte > 0; --byte) {
      out[offsets + i * offSize + byte - 1] = static_cast<char>(value & 0xFF);
      value >>= 8;
    }
  }
  out.replace(base + 1, data.size(), data);
  std::fill(out.begin() + static_cast<std::ptrdiff_t>(base + 1 + data.size()),
            out.begin() + static_cast<std::ptrdiff_t>(base + 1 + room), '\0');
  return out;
}

}
