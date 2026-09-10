#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

// Cutting the outlines out of a CFF table.
//
// Its own unit because a CFF is a format inside a format, with a vocabulary
// nothing else in this tree uses: an INDEX is a count, an offset size and a
// one-based offset array; a DICT is operands followed by their operator, with
// five ways to encode an integer; and the offsets in the Top DICT are absolute
// into the table. Keeping that beside the sfnt-level work in `SfntSubset.cpp`
// meant one file speaking two languages, and the sfnt half is the one a reader
// after "which tables are kept" is looking for.
//
// The interface is one function on purpose. See `SfntSubset.h` for what is cut
// and, more importantly, for what deliberately is not: this rebuilds the
// charstring INDEX **inside the byte range it already occupied**, because a
// CFF's Top DICT holds absolute offsets to its charset, its encoding and its
// private DICT, and shortening the INDEX would move every one of them.
// Rewriting those is a CFF writer, and `docs/tech-debt.md` TD-44 is where that
// decision is written down.
namespace microcore::pdf {

// The table with every glyph outside `kept` reduced to a single `endchar` --
// a glyph that draws nothing -- and the room that frees up zero filled where
// it stands, so nothing in the table moves.
//
// Empty when the table is not a shape this understands, which is the caller's
// signal to embed the font whole.
std::string subsetCffCharstrings(std::string_view cff, const std::set<std::uint16_t>& kept);

}
