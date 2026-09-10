#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// The pair kerning a font carries in its `GPOS` table.
//
// **Why GPOS and not `kern`.** Neither of the faces vendored here has a `kern`
// table at all: Inter and JetBrains Mono are both OpenType, and OpenType puts
// pair kerning in `GPOS` under the `kern` feature. A `kern`-table reader would
// have been simpler and would have kerned nothing.
//
// **What this is not.** It is not a shaper. It reads exactly one feature and
// exactly one lookup type -- pair adjustment -- and only the horizontal
// advance of the *first* glyph of a pair, which is what kerning is and is all
// a PDF's `TJ` array can express. Ligatures (`liga`, a `GSUB` feature),
// cursive attachment, mark positioning and contextual kerning are all
// deliberately absent: a PDF places one run of glyphs at one position, and
// anything that moves a glyph in y or substitutes one glyph for two cannot be
// said in that language without splitting the run, which would make the file's
// own line breaks disagree with the widths the layout computed.
//
// **Offsets, not views.** `parse` records offsets into the GPOS table and
// every lookup takes that table's bytes again. The alternative is holding a
// `std::string_view` into the font's buffer, which is the one thing
// `SfntFont::Table` exists to avoid: the font is returned by move, and a view
// into a moved-from `std::string` is valid only by luck.
namespace microcore::pdf {

class KernPairs {
public:
  // `gpos` is the GPOS table's bytes. Records where the `kern` feature's pair
  // adjustment subtables are; a font with no such feature is left empty and
  // every lookup then costs one comparison.
  void parse(std::string_view gpos);

  bool empty() const { return subtables_.empty(); }

  // The adjustment to `left`'s advance when `right` follows it, in the font's
  // own units -- negative tightens, which is what almost all kerning is. Zero
  // when the pair is not kerned, which is the overwhelming majority of pairs.
  //
  // Resolved per call rather than expanded into a table at parse time: class
  // based kerning is a `class1Count` by `class2Count` matrix, and Inter's is
  // large enough that materialising it would cost more than every lookup a
  // note ever makes.
  int adjustment(std::string_view gpos, std::uint16_t left, std::uint16_t right) const;

private:
  // Offsets from the start of the GPOS table to each `PairPos` subtable of
  // every lookup the `kern` feature names. In feature order, because the
  // format says the first lookup that matches a pair wins.
  std::vector<std::uint32_t> subtables_;
};

}
