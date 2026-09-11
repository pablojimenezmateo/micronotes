#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Writing big-endian numbers into a byte string.
//
// The mirror of `SfntRead.h`, and here for the reason that header gives about
// its readers: a second copy is a second chance to get one of the shifts wrong
// in only one of them. There were three copies of the 32-bit write, in two
// files -- the font subsetter builds sfnt tables and the deflate writer builds
// a zlib header, and both are "four bytes, most significant first".
//
// Its own header rather than an addition to `SfntRead.h` because `sfnt::` would
// be a lie about the zlib caller: a zlib stream is not a font, and the two
// share the byte order and nothing else. Unlike the readers these are not
// bounds checked, because a writer owns the buffer it is appending to --
// `writeU32At` is the one that does not, and it is the one that checks.
namespace microcore::pdf {

inline void appendU16(std::string& out, std::uint16_t value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

inline void appendU32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

// Overwrites four bytes already in `out`. Silently does nothing when they are
// not there: the callers patch a length back into a header they wrote
// themselves, so an out-of-range `at` is a bug in this file rather than
// anything a font could cause.
inline void writeU32At(std::string& out, std::size_t at, std::uint32_t value) {
  if(at + 4 > out.size()) return;
  out[at] = static_cast<char>((value >> 24) & 0xFF);
  out[at + 1] = static_cast<char>((value >> 16) & 0xFF);
  out[at + 2] = static_cast<char>((value >> 8) & 0xFF);
  out[at + 3] = static_cast<char>(value & 0xFF);
}

}
