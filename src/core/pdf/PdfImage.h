#pragma once

#include "core/pdf/PdfWriter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace microcore::pdf {

// A picture ready to become an `/XObject`.
//
// Two shapes, and which one a file takes is the difference between a PDF that
// is the size of the pictures in it and one several times that. A JPEG is
// *already* the compression a PDF's `DCTDecode` filter speaks, so its bytes
// go into the file untouched -- no decode, no re-encode, no generation loss.
// Anything else has to be decoded to samples and deflated.
struct PdfImage {
  int width = 0;
  int height = 0;
  // 1 for grey, 3 for colour. Nothing else is produced here: CMYK is refused
  // rather than converted, because a wrong colour conversion is worse than a
  // missing picture.
  int components = 3;
  // The stream body. JPEG bytes when `jpeg`, otherwise packed 8-bit samples,
  // `components` per pixel, rows top to bottom.
  std::string data;
  bool jpeg = false;
  // One byte of coverage per pixel, or empty. A PNG with transparency in it
  // becomes an opaque image plus this, which is what `/SMask` is: PDF has no
  // per-pixel alpha in an image, only a second image that says how much of the
  // first to let through.
  std::string alpha;

  bool valid() const { return width > 0 && height > 0 && !data.empty(); }
};

// What a JPEG's own header says it is.
struct JpegInfo {
  int width = 0;
  int height = 0;
  int components = 0;
  // Progressive scans are not what `DCTDecode` is defined over. Most viewers
  // take one anyway; a file that only *most* readers can open is not a file
  // this writes, so a progressive JPEG is decoded like a PNG instead.
  bool progressive = false;
};

// Reads the dimensions out of a JPEG without decoding it, so its bytes can be
// passed straight through. `std::nullopt` when the bytes are not a JPEG or the
// header is truncated.
std::optional<JpegInfo> readJpegInfo(std::string_view bytes);

// Writes the image and its soft mask, and returns the object a page's
// `/XObject` resource names.
ObjectId writeImage(PdfWriter& writer, const PdfImage& image);

}
