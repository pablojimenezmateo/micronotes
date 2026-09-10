#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A font file cut down to the glyphs a document actually shows.
//
// **Glyph numbers do not move.** The exporter encodes text as `Identity-H`
// with an `/Identity` CID-to-GID map, so a glyph's number in the content
// stream *is* its number in the font program. That single fact is what makes
// this a filter rather than the font toolchain `docs/tech-debt.md` used to say
// it needed: nothing is renumbered, so no composite glyph's component
// references have to be rewritten, no `charset` has to be rebuilt, and the
// widths array and `ToUnicode` map the font already wrote still describe the
// right glyphs.
//
// Two things happen, and they are different in kind.
//
//   * **Tables a PDF has no use for are dropped.** `GPOS`, `GSUB` and `GDEF`
//     are the shaper's, and there is no shaper on the other side of a PDF: the
//     pair kerning they carry is already resolved into the `TJ` arrays of the
//     content stream by `PdfFont`. `name`, `post` and the rest are metadata
//     about a font nobody will install. This shrinks the font program itself.
//
//   * **The outlines of glyphs nothing showed are removed or blanked.** For a
//     `glyf` face this is a real rebuild -- the kept glyphs' data, and an
//     empty `loca` entry for every other glyph -- so the table shrinks to what
//     the note used. For a CFF face the charstrings are *blanked in place*
//     instead: each unused one becomes a single `endchar` and a run of zero
//     bytes, so every offset in the font stays exactly where it was. See below
//     for why that is the choice.
//
// **Why CFF is blanked rather than rebuilt.** A CFF's Top DICT holds absolute
// offsets to its charset, its encoding, its charstrings and its private DICTs,
// so shortening the charstring INDEX moves all of them and a real CFF subset
// is a re-layout of the table rather than a filter over it. `CffSubset.h` is
// where that decision is explained and `docs/tech-debt.md` TD-44 is what it
// still costs. The short version: the cost being paid here is *file size*, the
// stream is `FlateDecode`d, and a blanked charstring is a run of one repeated
// byte -- so a note's PDF comes out near the size a real subset would make it.
// What blanking does not shrink is what the *viewer* decompresses.
namespace microcore::pdf {

// The font with only `glyphs` (and glyph 0, and any component a kept
// composite glyph is built from) drawable, and only the tables a PDF reads.
//
// Empty when the file is not a shape this understands, which is the caller's
// signal to embed the whole thing -- a font that cannot be cut down is a big
// file, and a font that has been cut down wrongly is a page of blank boxes.
std::string subsetFont(std::string_view font, const std::vector<std::uint16_t>& glyphs);

}
