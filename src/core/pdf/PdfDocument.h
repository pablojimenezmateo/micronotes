#pragma once

#include "core/pdf/PdfFont.h"
#include "core/pdf/PdfImage.h"
#include "core/pdf/PdfWriter.h"
#include "core/pdf/Sfnt.h"

#include <cstddef>
#include <string>
#include <vector>

namespace microcore::pdf {

// A whole PDF: its pages, the faces they are set in and the pictures they
// show.
//
// Fonts and images are registered once for the document and named from a
// content stream by slot -- `/F0`, `/Im3`. They are shared by every page
// through one resource dictionary on the page tree, which the format lets
// pages inherit. A per-page dictionary would be smaller by a few dozen bytes
// and would require every page to declare what it used, which is a list that
// can be wrong; an inherited one cannot be.
//
// Order matters in one place: the fonts are written by `finish`, after every
// page has been composed, because a font's widths array and its `ToUnicode`
// map are built from the glyphs the pages actually asked it to encode.
class PdfDocument {
public:
  // One live rectangle on a page, and what activating it does.
  //
  // A page is written as a content stream, but an annotation is a property of
  // the page *object* -- so it cannot be emitted by whoever painted the rect
  // and has to arrive with the page. That is why `addPage` takes a list of
  // these rather than the writer growing a `link` call beside `text`.
  //
  // Coordinates are measured **from the top of the page**, which is what every
  // coordinate handed to `PdfContent` is measured from, and for the same
  // reason: one flip, in one expression, rather than an inversion that is
  // right in nine call sites and wrong in the tenth.
  //
  // `uri` is the only action the writer emits today. A named destination
  // inside the document -- what an outline entry or an internal link wants --
  // is a second field here rather than a second interface, which is why this
  // is an annotation and not a link.
  struct Annotation {
    float x = 0.0f;
    // The rect's top edge, measured down from the top of the page.
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::string uri;
  };

  // What the document says about itself. Every field is optional, and
  // `creationDate` is a PDF date string (`D:YYYYMMDDHHmmSS`) rather than a
  // time the writer reads for itself -- so a test can produce a byte-identical
  // file twice, which is the only way to diff one export against another.
  struct Info {
    std::string title;
    std::string author;
    std::string creator;
    std::string creationDate;
  };

  // The slot a content stream names the face by. Registering the same file
  // twice registers two faces; the caller is expected to keep one per face.
  int addFont(SfntFont font, std::string baseName);
  PdfFont& font(int slot) { return fonts_[static_cast<std::size_t>(slot)]; }
  int fontCount() const { return static_cast<int>(fonts_.size()); }

  // Zero images is the common case -- most notes have no pictures -- so this
  // costs nothing until one does.
  int addImage(PdfImage image);

  // Sizes are in points, which is what a PDF measures everything in: 72 to
  // the inch, so A4 is 595.28 by 841.89.
  void addPage(float width, float height, std::string content,
               std::vector<Annotation> annotations = {});
  std::size_t pageCount() const { return pages_.size(); }

  std::string finish(const Info& info);

private:
  struct Page {
    float width = 0.0f;
    float height = 0.0f;
    std::string content;
    std::vector<Annotation> annotations;
  };

  std::vector<PdfFont> fonts_;
  std::vector<PdfImage> images_;
  std::vector<Page> pages_;
};

}
