#pragma once

#include "core/pdf/PdfWriter.h"
#include "core/pdf/Sfnt.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace microcore::pdf {

// One embedded face, and the only thing in the writer that knows text is made
// of characters.
//
// Two jobs, and they are one object because they must agree. It *measures* a
// string, which is what the layout above it breaks lines with; and it
// *encodes* that same string into the glyph numbers the page's content stream
// shows. A measurement taken from one font and an encoding written from
// another is text that overruns the line breaks computed for it, so both come
// from the same `SfntFont` and neither can be asked for separately.
//
// Encoding is `Identity-H`: the bytes in the content stream are glyph numbers,
// two bytes each, not characters. That is what lets a note in any language
// export at all -- a single-byte encoding covers Latin-1 and nothing else --
// and it is why `ToUnicode` below is not optional. Without that map the text
// in the file is a sequence of numbers meaningful only to this exact font, and
// copying a paragraph out of the PDF yields nonsense.
//
// Kerned, and not ligated. The width of a run is the sum of its glyphs'
// advances plus the pair adjustments the face asks for -- read straight out of
// GPOS by `SfntFont::kerning` -- and the same adjustments are written into the
// content stream as the numbers of a `TJ` array. Measuring one way and drawing
// the other is the failure this class exists to make impossible, so there is
// no way to ask for one without the other.
//
// Ligatures are a different thing and still absent: `fi` is two glyphs on the
// page as it is two glyphs on screen. Substituting one glyph for two would
// change how many glyphs a run is made of, which is a shaping decision, and
// the reading pane does not make it either.
class PdfFont {
public:
  PdfFont(SfntFont font, std::string baseName);

  // The width of `text` set at `size` points.
  float width(std::string_view text, float size) const;

  // `text` as the array a `TJ` operator takes -- runs of glyph numbers with
  // the pair adjustments between them -- with every glyph it used recorded so
  // the widths array and the `ToUnicode` map describe exactly the glyphs the
  // file actually shows.
  //
  // A `TJ` array rather than a `Tj` string even for an unkerned run, which
  // costs two brackets in a stream that is then compressed. One operator is
  // worth more than the bytes: two would be two paths through the placement
  // arithmetic, and the one taken less often is the one that would be wrong.
  std::string encode(std::string_view text);

  // Where the top of a line sits above its baseline, and the bottom below it,
  // at `size` points. The composer places baselines; the layout above it
  // thinks in line boxes, and this is the conversion between them.
  float ascent(float size) const;
  float descent(float size) const;

  // Writes the font's objects and returns the one a page's `/Font` resource
  // names. Call after every page is composed: the widths and the `ToUnicode`
  // map are built from what was encoded, so a font written early would
  // describe only the glyphs seen so far.
  ObjectId write(PdfWriter& writer);

  bool used() const { return !glyphs_.empty(); }
  const SfntFont& sfnt() const { return sfnt_; }

private:
  std::string widthsArray() const;
  std::string toUnicodeCMap() const;

  SfntFont sfnt_;
  std::string baseName_;
  // Every glyph shown, and a code point that produced it. Ordered, because the
  // widths array and the CMap are both written in glyph order and an
  // unordered map would reorder the file between runs of the same export.
  //
  // One code point per glyph, not all of them: two characters that map to the
  // same glyph are indistinguishable in the output anyway, and the map only
  // has to say what a reader copying text out should get back.
  std::map<std::uint16_t, char32_t> glyphs_;
};

}
