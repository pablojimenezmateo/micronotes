#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Reading big-endian numbers out of a font file, bounds checked.
//
// Every number in an sfnt is big-endian, and every read of one is checked
// against the *buffer* rather than against the table that claimed to contain
// it. A font file is data from disk: a truncated one must produce a font that
// measures badly, not a read past the end of a string. Out of range reads as
// zero, which for every field in every table below is either a harmless value
// or one that fails the shape check above it.
//
// Its own header because two units read these: `Sfnt.cpp` for the metrics and
// the character map, and `SfntKern.cpp` for the GPOS table. They were one
// anonymous namespace's worth of helpers, and a second copy of a bounds check
// is a second chance to get the `+ 1` wrong in only one of them.
namespace microcore::pdf::sfnt {

inline std::uint8_t u8At(std::string_view data, std::size_t at) {
  return at < data.size() ? static_cast<std::uint8_t>(data[at]) : 0;
}

inline std::uint16_t u16At(std::string_view data, std::size_t at) {
  if(at + 1 >= data.size()) return 0;
  return static_cast<std::uint16_t>((u8At(data, at) << 8) | u8At(data, at + 1));
}

inline std::int16_t s16At(std::string_view data, std::size_t at) {
  return static_cast<std::int16_t>(u16At(data, at));
}

inline std::uint32_t u32At(std::string_view data, std::size_t at) {
  if(at + 3 >= data.size()) return 0;
  return (static_cast<std::uint32_t>(u8At(data, at)) << 24) |
         (static_cast<std::uint32_t>(u8At(data, at + 1)) << 16) |
         (static_cast<std::uint32_t>(u8At(data, at + 2)) << 8) |
         static_cast<std::uint32_t>(u8At(data, at + 3));
}

}
