#pragma once

#include "core/pdf/PdfDocument.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace micronotes::exporting {

// Turns an `![alt](target)` into something a page can show.
//
// The same resolution rule the reading pane uses -- `AttachmentService`, over
// the library root -- so a picture that draws on screen is a picture that
// exports, and one that does not is missing from both.
//
// The interesting decision is what happens to the file's bytes. A JPEG is
// already the compression a PDF's `DCTDecode` filter reads, so its bytes go
// into the file untouched: no decode, no re-encode, no second generation of
// JPEG artefacts, and a page of photographs costs what the photographs cost.
// Anything else is decoded to samples and deflated, which for a screenshot is
// both bigger than the PNG it came from and the only thing PDF can be handed.
class PdfPictures {
public:
  PdfPictures(microcore::pdf::PdfDocument& document, std::filesystem::path libraryRoot)
    : document_(&document), root_(std::move(libraryRoot)) {}

  // What a target resolved to. A slot of -1 means nothing drawable, which is
  // a remote target, a file outside the library, a format that cannot be
  // decoded, or a build with no image decoder in it.
  struct Picture {
    int slot = -1;
    // The size the picture wants on the page, in points. Pixels are read as
    // 96 to the inch -- the density a screen is assumed to be -- so a picture
    // takes about as much of the printed column as it takes of the reading
    // pane, rather than the four times as much a point-per-pixel reading
    // would give it.
    float width = 0.0f;
    float height = 0.0f;

    bool drawable() const { return slot >= 0 && width > 0.0f && height > 0.0f; }
  };

  // Memoised on the target, because the layout asks once per picture per
  // relaid block and decoding a screenshot twice is the whole cost of the
  // export.
  const Picture& resolve(std::string_view target);

private:
  microcore::pdf::PdfDocument* document_ = nullptr;
  std::filesystem::path root_;
  std::unordered_map<std::string, Picture> resolved_;
};

}
