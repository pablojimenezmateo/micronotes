#include "core/pdf/SfntSubset.h"

#include "core/pdf/CffSubset.h"
#include "core/pdf/SfntRead.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace microcore::pdf {
namespace {

using sfnt::u16At;
using sfnt::u32At;
using sfnt::u8At;

// The tables a PDF reads, by the shape of the font program.
//
// For a `glyf` face this is what the format specification asks a subsetted
// `/FontFile2` to carry, and nothing else: `cmap` is not among them because
// the CID-to-GID map is `/Identity`, so nothing on the other side ever looks a
// character up.
constexpr const char* kTrueTypeTables[] = {"head", "hhea", "maxp", "hmtx",
                                           "loca", "glyf", "cvt ", "fpgm", "prep"};

// For a CFF face the stream is declared `/Subtype /OpenType`, so it has to
// stay a valid OpenType file -- which means keeping the tables that
// specification calls required even where a PDF viewer would not read them.
// `cmap` is the expensive one of those and is kept for exactly that reason.
constexpr const char* kCffTables[] = {"CFF ", "cmap", "head", "hhea",
                                      "hmtx", "maxp", "name", "OS/2", "post"};

struct TableRef {
  std::string tag;
  std::string_view data;
};

// A table's bytes, or empty. Bounds checked against the buffer, because a font
// file is data from disk.
std::string_view tableIn(std::string_view font, const char* tag) {
  const std::uint16_t count = u16At(font, 4);
  for(std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = 12 + static_cast<std::size_t>(i) * 16;
    if(record + 16 > font.size()) break;
    if(std::memcmp(font.data() + record, tag, 4) != 0) continue;
    const std::uint32_t offset = u32At(font, record + 8);
    const std::uint32_t length = u32At(font, record + 12);
    if(offset >= font.size()) return {};
    return font.substr(offset, std::min<std::size_t>(length, font.size() - offset));
  }
  return {};
}

// A table's checksum: the sum of its 32-bit words, with the tail zero padded.
std::uint32_t checksum(std::string_view data) {
  std::uint32_t sum = 0;
  for(std::size_t at = 0; at < data.size(); at += 4) sum += u32At(data, at);
  return sum;
}

void appendU16(std::string& out, std::uint16_t value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

void appendU32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

void writeU32At(std::string& out, std::size_t at, std::uint32_t value) {
  if(at + 4 > out.size()) return;
  out[at] = static_cast<char>((value >> 24) & 0xFF);
  out[at + 1] = static_cast<char>((value >> 16) & 0xFF);
  out[at + 2] = static_cast<char>((value >> 8) & 0xFF);
  out[at + 3] = static_cast<char>(value & 0xFF);
}

// A whole sfnt built from the tables given: the directory, in the tag order
// the format asks for, with every table padded to a four-byte boundary.
//
// `head`'s `checkSumAdjustment` is recomputed at the end, because it is a
// checksum *of the whole file* and so cannot be known until the file exists.
std::string assemble(std::uint32_t version, std::vector<TableRef> tables) {
  std::sort(tables.begin(), tables.end(),
            [](const TableRef& a, const TableRef& b) { return a.tag < b.tag; });

  const auto count = static_cast<std::uint16_t>(tables.size());
  // `searchRange` and the two beside it are derived values no reader uses and
  // every reader checks nothing about. Written as the format defines them.
  std::uint16_t power = 1;
  std::uint16_t log2 = 0;
  while(power * 2u <= count) {
    power = static_cast<std::uint16_t>(power * 2);
    ++log2;
  }
  std::string out;
  appendU32(out, version);
  appendU16(out, count);
  appendU16(out, static_cast<std::uint16_t>(power * 16));
  appendU16(out, log2);
  appendU16(out, static_cast<std::uint16_t>(count * 16 - power * 16));

  const std::size_t directory = out.size();
  out.append(static_cast<std::size_t>(count) * 16, '\0');

  std::size_t headOffset = 0;
  for(std::size_t i = 0; i < tables.size(); ++i) {
    const std::size_t record = directory + i * 16;
    const auto offset = static_cast<std::uint32_t>(out.size());
    out.replace(record, 4, tables[i].tag);
    writeU32At(out, record + 4, checksum(tables[i].data));
    writeU32At(out, record + 8, offset);
    writeU32At(out, record + 12, static_cast<std::uint32_t>(tables[i].data.size()));
    if(tables[i].tag == "head") headOffset = offset;
    out.append(tables[i].data);
    while((out.size() % 4) != 0) out.push_back('\0');
  }

  if(headOffset != 0 && headOffset + 12 <= out.size()) {
    writeU32At(out, headOffset + 8, 0);
    const std::uint32_t whole = checksum(out);
    writeU32At(out, headOffset + 8, 0xB1B0AFBAu - whole);
  }
  return out;
}

// A `cmap` that maps nothing, in the format every subsetter emits for one.
//
// A character map is what turns a code point into a glyph, and there is no
// code point on the other side of this: the content stream holds glyph numbers
// and the CID-to-GID map is `/Identity`, so nothing a viewer does with this
// font ever consults it. A `glyf` face therefore carries no `cmap` at all --
// the format specification's list of tables a subsetted `/FontFile2` needs
// leaves it out. A CFF face is embedded as `/Subtype /OpenType` and so has to
// stay a valid OpenType file, which does require one, and the original is 26
// KB of mapping for 2,700 glyphs.
//
// So: one format 4 subtable with a single segment ending at `0xFFFF`, which is
// the shape the format defines for "no characters are mapped". Thirty two
// bytes instead of twenty six thousand.
std::string emptyCmap() {
  std::string out;
  appendU16(out, 0);   // version
  appendU16(out, 1);   // one encoding record
  appendU16(out, 3);   // Windows
  appendU16(out, 1);   // Unicode BMP
  appendU32(out, 12);  // where the subtable starts

  appendU16(out, 4);   // format
  appendU16(out, 24);  // its length
  appendU16(out, 0);   // language
  appendU16(out, 2);   // segCountX2
  appendU16(out, 2);   // searchRange
  appendU16(out, 0);   // entrySelector
  appendU16(out, 0);   // rangeShift
  appendU16(out, 0xFFFF);  // endCode[0]
  appendU16(out, 0);       // the required padding between the two arrays
  appendU16(out, 0xFFFF);  // startCode[0]
  appendU16(out, 1);       // idDelta[0]
  appendU16(out, 0);       // idRangeOffset[0]
  return out;
}

// ---- glyf ------------------------------------------------------------------

// Where a glyph's outline data sits in `glyf`, read off `loca`.
struct GlyphRange {
  std::uint32_t from = 0;
  std::uint32_t to = 0;
};

GlyphRange glyphRange(std::string_view loca, bool longFormat, std::uint16_t glyph) {
  if(longFormat) {
    return {u32At(loca, static_cast<std::size_t>(glyph) * 4),
            u32At(loca, static_cast<std::size_t>(glyph) * 4 + 4)};
  }
  // Short `loca` stores halved offsets, which is why it needs even ones.
  return {static_cast<std::uint32_t>(u16At(loca, static_cast<std::size_t>(glyph) * 2)) * 2u,
          static_cast<std::uint32_t>(u16At(loca, static_cast<std::size_t>(glyph) * 2 + 2)) * 2u};
}

// Every component a composite glyph is built from, added to `kept`.
//
// Transitively: a composite may name another composite. Without this an
// accented letter exports as the bare letter with nothing over it, or as
// nothing at all -- and only for the letters that happen to be composites,
// which is the kind of gap that reaches a reader rather than a test.
void addComponents(std::string_view glyf, std::string_view loca, bool longFormat,
                   std::uint16_t glyphCount, std::uint16_t glyph, std::set<std::uint16_t>& kept) {
  const GlyphRange range = glyphRange(loca, longFormat, glyph);
  if(range.to <= range.from || range.to > glyf.size()) return;
  if(range.to - range.from < 10) return;
  if(static_cast<std::int16_t>(u16At(glyf, range.from)) >= 0) return;

  std::size_t at = range.from + 10;
  for(;;) {
    if(at + 4 > range.to) return;
    const std::uint16_t flags = u16At(glyf, at);
    const std::uint16_t component = u16At(glyf, at + 2);
    at += 4;
    at += (flags & 0x0001) != 0 ? 4 : 2;             // ARG_1_AND_2_ARE_WORDS
    if((flags & 0x0008) != 0) at += 2;               // WE_HAVE_A_SCALE
    else if((flags & 0x0040) != 0) at += 4;          // X_AND_Y_SCALE
    else if((flags & 0x0080) != 0) at += 8;          // TWO_BY_TWO
    if(component < glyphCount && kept.insert(component).second) {
      addComponents(glyf, loca, longFormat, glyphCount, component, kept);
    }
    if((flags & 0x0020) == 0) return;                // MORE_COMPONENTS
  }
}

// `glyf` and a long-format `loca` holding only the kept glyphs. A dropped
// glyph gets an empty entry, which is what the format says a glyph with no
// outline is -- a space is one in every font ever made.
struct Outlines {
  std::string glyf;
  std::string loca;
};

Outlines rebuildOutlines(std::string_view glyf, std::string_view loca, bool longFormat,
                         std::uint16_t glyphCount, const std::set<std::uint16_t>& kept) {
  Outlines out;
  out.loca.reserve((static_cast<std::size_t>(glyphCount) + 1) * 4);
  for(std::uint16_t glyph = 0; glyph < glyphCount; ++glyph) {
    appendU32(out.loca, static_cast<std::uint32_t>(out.glyf.size()));
    if(kept.count(glyph) == 0) continue;
    const GlyphRange range = glyphRange(loca, longFormat, glyph);
    if(range.to <= range.from || range.to > glyf.size()) continue;
    out.glyf.append(glyf.substr(range.from, range.to - range.from));
    // Every glyph starts on a four-byte boundary. Not required by the format
    // for a long `loca`, but a reader that assumes it is a reader that has
    // been fed nothing else.
    while((out.glyf.size() % 4) != 0) out.glyf.push_back('\0');
  }
  appendU32(out.loca, static_cast<std::uint32_t>(out.glyf.size()));
  return out;
}

}

std::string subsetFont(std::string_view font, const std::vector<std::uint16_t>& glyphs) {
  if(font.size() < 12) return {};
  const std::uint32_t version = u32At(font, 0);
  // `OTTO` is OpenType with CFF outlines; the other two are TrueType. A
  // collection (`ttcf`) is not one of them, which is the caller's cue to embed
  // whatever it has whole.
  const bool cff = version == 0x4F54544Fu;
  if(!cff && version != 0x00010000u && version != 0x74727565u) return {};

  const std::string_view maxp = tableIn(font, "maxp");
  const std::string_view head = tableIn(font, "head");
  if(maxp.size() < 6 || head.size() < 54) return {};
  const std::uint16_t glyphCount = u16At(maxp, 4);
  if(glyphCount == 0) return {};

  // Glyph 0 is `.notdef`, which a viewer draws for anything it cannot place
  // and which the format requires be present.
  std::set<std::uint16_t> kept {0};
  for(const std::uint16_t glyph : glyphs) {
    if(glyph < glyphCount) kept.insert(glyph);
  }

  // The tables this call *replaces*. Everything else in the keep list below is
  // copied out of the original, so the two halves of the answer -- which
  // tables survive and which of those are rewritten -- stay separate: a table
  // added to a keep list and forgotten here is copied, which is right, rather
  // than silently dropped.
  std::map<std::string, std::string, std::less<>> rewritten;
  const char* const* keep = nullptr;
  std::size_t keepCount = 0;

  if(cff) {
    std::string program = subsetCffCharstrings(tableIn(font, "CFF "), kept);
    if(program.empty()) return {};
    rewritten.emplace("CFF ", std::move(program));
    rewritten.emplace("cmap", emptyCmap());
    keep = kCffTables;
    keepCount = std::size(kCffTables);
  } else {
    const std::string_view glyf = tableIn(font, "glyf");
    const std::string_view loca = tableIn(font, "loca");
    if(glyf.empty() || loca.empty()) return {};
    const bool longFormat = u16At(head, 50) == 1;
    // Over a copy, because closing the set adds to it while it is walked.
    for(const std::uint16_t glyph : std::set<std::uint16_t>(kept)) {
      addComponents(glyf, loca, longFormat, glyphCount, glyph, kept);
    }
    Outlines outlines = rebuildOutlines(glyf, loca, longFormat, glyphCount, kept);
    rewritten.emplace("glyf", std::move(outlines.glyf));
    rewritten.emplace("loca", std::move(outlines.loca));
    // A long `loca` always, whatever the original was: the short form stores
    // halved offsets and so needs every glyph to start on an even byte, which
    // is one more invariant to hold for no gain at this size.
    std::string patched(head);
    patched[50] = '\0';
    patched[51] = '\1';
    rewritten.emplace("head", std::move(patched));
    keep = kTrueTypeTables;
    keepCount = std::size(kTrueTypeTables);
  }

  std::vector<TableRef> tables;
  tables.reserve(keepCount);
  for(std::size_t i = 0; i < keepCount; ++i) {
    const std::string_view tag(keep[i]);
    const auto replacement = rewritten.find(tag);
    const std::string_view data =
      replacement != rewritten.end() ? std::string_view(replacement->second) : tableIn(font, keep[i]);
    // A table the original does not have is simply absent: `cvt `, `fpgm` and
    // `prep` are only in a hinted TrueType face, and an empty one would be a
    // directory entry pointing at nothing.
    if(data.empty()) continue;
    tables.push_back({std::string(tag), data});
  }
  if(tables.empty()) return {};
  return assemble(version, std::move(tables));
}

}
