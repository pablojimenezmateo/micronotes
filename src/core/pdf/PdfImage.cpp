#include "core/pdf/PdfImage.h"

#include <cstddef>

namespace microcore::pdf {
namespace {

std::uint8_t byteAt(std::string_view bytes, std::size_t at) {
  return at < bytes.size() ? static_cast<std::uint8_t>(bytes[at]) : 0;
}

// Whether a marker introduces a start-of-frame segment, which is the only kind
// that carries the dimensions. The three holes in the range are other things
// that happen to sit inside it: a Huffman table, an arithmetic coding table,
// and a reserved marker.
bool isStartOfFrame(std::uint8_t marker) {
  if(marker < 0xC0 || marker > 0xCF) return false;
  return marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

}

std::optional<JpegInfo> readJpegInfo(std::string_view bytes) {
  if(bytes.size() < 4 || byteAt(bytes, 0) != 0xFF || byteAt(bytes, 1) != 0xD8) return std::nullopt;
  std::size_t at = 2;
  while(at + 3 < bytes.size()) {
    // Markers may be preceded by any number of fill bytes.
    if(byteAt(bytes, at) != 0xFF) {
      ++at;
      continue;
    }
    const std::uint8_t marker = byteAt(bytes, at + 1);
    if(marker == 0xFF) {
      ++at;
      continue;
    }
    at += 2;
    // Standalone markers carry no length: the restart markers, and the two
    // that bracket the stream.
    if(marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if(at + 1 >= bytes.size()) return std::nullopt;
    const std::size_t length = (static_cast<std::size_t>(byteAt(bytes, at)) << 8) |
                               byteAt(bytes, at + 1);
    if(length < 2 || at + length > bytes.size()) return std::nullopt;
    if(isStartOfFrame(marker)) {
      if(length < 8) return std::nullopt;
      JpegInfo info;
      info.height = static_cast<int>((byteAt(bytes, at + 3) << 8) | byteAt(bytes, at + 4));
      info.width = static_cast<int>((byteAt(bytes, at + 5) << 8) | byteAt(bytes, at + 6));
      info.components = byteAt(bytes, at + 7);
      info.progressive = marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE;
      if(info.width <= 0 || info.height <= 0) return std::nullopt;
      return info;
    }
    // The scan is the last thing before the entropy-coded data, and the
    // dimensions are always declared before it.
    if(marker == 0xDA) return std::nullopt;
    at += length;
  }
  return std::nullopt;
}

ObjectId writeImage(PdfWriter& writer, const PdfImage& image) {
  if(!image.valid()) return 0;
  const ObjectId id = writer.reserve();
  ObjectId mask = 0;
  if(!image.alpha.empty()) {
    mask = writer.reserve();
    std::string maskDict = "/Type /XObject /Subtype /Image ";
    maskDict += "/Width " + std::to_string(image.width) + " ";
    maskDict += "/Height " + std::to_string(image.height) + " ";
    maskDict += "/ColorSpace /DeviceGray /BitsPerComponent 8";
    writer.stream(mask, maskDict, image.alpha, true);
  }

  std::string dict = "/Type /XObject /Subtype /Image ";
  dict += "/Width " + std::to_string(image.width) + " ";
  dict += "/Height " + std::to_string(image.height) + " ";
  dict += image.components == 1 ? "/ColorSpace /DeviceGray " : "/ColorSpace /DeviceRGB ";
  dict += "/BitsPerComponent 8";
  if(mask > 0) dict += " /SMask " + std::to_string(mask) + " 0 R";
  if(image.jpeg) dict += " /Filter /DCTDecode";
  // JPEG bytes are already compressed; running deflate over them costs time
  // and gains nothing, and the filter is named above rather than by `stream`.
  writer.stream(id, dict, image.data, !image.jpeg);
  return id;
}

}
