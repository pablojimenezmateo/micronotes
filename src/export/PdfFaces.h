#pragma once

#include "core/pdf/PdfDocument.h"
#include "doc/Layout.h"
#include "ui/Fonts.h"

#include <array>
#include <string_view>

namespace micronotes::exporting {

// The faces a note is set in, embedded once for the whole document.
//
// The same seven vendored files the screen draws with, found through
// `ui::faceFile` so that the export cannot be set in a different Inter from
// the one on screen. There is no bold italic monospace file, exactly as there
// is none for the screen, and it resolves to the bold the same way.
//
// A face is embedded only if something is actually set in it -- `PdfDocument`
// drops the unused ones -- so a note with no code in it carries no monospace
// font, and a note with no emphasis carries neither italic.
class PdfFaces {
public:
  // False when not even the regular sans face could be read, which is the one
  // case there is nothing to export with. Every other missing file falls back
  // to the regular of its own family, which is what the screen does too.
  bool load(microcore::pdf::PdfDocument& document);

  // The document slot a run's style is set in.
  int slot(const doc::RunStyle& style) const;
  int slot(bool mono, bool strong, bool italic) const;

  microcore::pdf::PdfFont& font(microcore::pdf::PdfDocument& document,
                                const doc::RunStyle& style) const {
    return document.font(slot(style));
  }

private:
  // Indexed by `(mono << 2) | (strong << 1) | italic`, which is why the three
  // are packed rather than a struct: the table is a lookup and not a search.
  std::array<int, 8> slots_ {};
  bool loaded_ = false;
};

// The print type scale: the screen's, in points.
//
// Deliberately *not* `ui::type()`, which carries the reader's text-size
// preference. That preference is about how a screen is being read at arm's
// length; it has nothing to say about a page that is going to be printed or
// sent to somebody else, and an export that changed size with it would produce
// a different document for two people looking at the same note.
doc::TypeMetrics printTypeMetrics();

// The layout options a page of this width is laid out with. The spacings are
// the layout's own defaults taken into points, so a list indents by the same
// fraction of its column as it does on screen.
doc::LayoutOptions printLayoutOptions(float width);

// What `doc::DocumentLayout` measures with: the embedded faces themselves.
//
// This is the point of the whole arrangement. The line breaks are computed
// from the advances of the very glyphs the file goes on to show, so the text
// cannot overrun the column it was broken to fit -- which is what happens the
// moment a document is measured with one font and drawn with another.
doc::Metrics printMetrics(const PdfFaces& faces, microcore::pdf::PdfDocument& document);

}
