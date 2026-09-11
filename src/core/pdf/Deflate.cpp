#include "core/pdf/Deflate.h"

#include "core/pdf/ByteOrder.h"

#include <algorithm>
#include <cstdint>

#if MICROCORE_HAS_ZLIB
#include <zlib.h>
#endif

namespace microcore::pdf {
namespace {

// Adler-32, which is the checksum a zlib stream ends with. Written out here
// rather than taken from zlib because the fallback path has to produce one
// without zlib being present -- that is the whole point of the fallback.
std::uint32_t adler32Of(std::string_view data) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for(const char byte : data) {
    a = (a + static_cast<std::uint8_t>(byte)) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

// Deflate's stored block type wrapped in a zlib header: no compression, but a
// stream every `FlateDecode` reader accepts.
std::string storedStream(std::string_view data) {
  std::string out;
  out.reserve(data.size() + data.size() / 65535u * 5u + 16u);
  // CMF: deflate, 32K window. FLG chosen so the pair is a multiple of 31,
  // which is the header check zlib readers apply.
  out.push_back(static_cast<char>(0x78));
  out.push_back(static_cast<char>(0x01));
  std::size_t at = 0;
  do {
    const std::size_t take = std::min<std::size_t>(data.size() - at, 65535u);
    const bool last = at + take >= data.size();
    out.push_back(static_cast<char>(last ? 0x01 : 0x00));
    const auto length = static_cast<std::uint16_t>(take);
    // LEN then its ones' complement, both little-endian.
    out.push_back(static_cast<char>(length & 0xFF));
    out.push_back(static_cast<char>((length >> 8) & 0xFF));
    out.push_back(static_cast<char>(~length & 0xFF));
    out.push_back(static_cast<char>((~length >> 8) & 0xFF));
    out.append(data.substr(at, take));
    at += take;
  } while(at < data.size());
  appendU32(out, adler32Of(data));
  return out;
}

}

bool deflateCompresses() {
#if MICROCORE_HAS_ZLIB
  return true;
#else
  return false;
#endif
}

std::string deflate(std::string_view data) {
#if MICROCORE_HAS_ZLIB
  uLongf bound = compressBound(static_cast<uLong>(data.size()));
  std::string out(bound, '\0');
  const int result = compress2(reinterpret_cast<Bytef*>(out.data()), &bound,
                               reinterpret_cast<const Bytef*>(data.data()),
                               static_cast<uLong>(data.size()), Z_BEST_COMPRESSION);
  // A failure here is out of memory or a bad argument, neither of which is
  // worth a second error path through every caller: the stored encoding is
  // always available and always correct.
  if(result != Z_OK) return storedStream(data);
  out.resize(bound);
  return out;
#else
  return storedStream(data);
#endif
}

}
