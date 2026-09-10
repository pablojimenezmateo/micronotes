#include "core/pdf/PdfFont.h"

#include "core/pdf/SfntSubset.h"

#include "core/util/Utf8.h"

#include <array>
#include <utility>
#include <vector>

namespace microcore::pdf {
namespace {

constexpr char kHex[] = "0123456789ABCDEF";

void appendHex16(std::string& out, unsigned value) {
  out.push_back(kHex[(value >> 12) & 0xF]);
  out.push_back(kHex[(value >> 8) & 0xF]);
  out.push_back(kHex[(value >> 4) & 0xF]);
  out.push_back(kHex[value & 0xF]);
}

// A code point as the UTF-16BE a `ToUnicode` CMap is written in. Anything past
// the BMP becomes a surrogate pair, which is the only reason this is not four
// hex digits and a comment.
void appendUtf16Hex(std::string& out, char32_t value) {
  if(value <= 0xFFFF) {
    appendHex16(out, static_cast<unsigned>(value));
    return;
  }
  const char32_t offset = value - 0x10000;
  appendHex16(out, static_cast<unsigned>(0xD800 + (offset >> 10)));
  appendHex16(out, static_cast<unsigned>(0xDC00 + (offset & 0x3FF)));
}

}

PdfFont::PdfFont(SfntFont font, std::string baseName)
  : sfnt_(std::move(font)), baseName_(std::move(baseName)) {}

float PdfFont::width(std::string_view text, float size) const {
  int units = 0;
  std::size_t at = 0;
  std::uint16_t previous = 0;
  bool havePrevious = false;
  while(at < text.size()) {
    const util::CodePoint point = util::decodeAt(text, at);
    const std::uint16_t glyph = sfnt_.glyph(point.value);
    // The pair adjustment belongs to the gap, so it is charged once, between
    // the two glyphs -- never before the first, which would move the run's
    // own origin away from where the layout placed it.
    if(havePrevious) units += sfnt_.kerning(previous, glyph);
    units += sfnt_.advance(glyph);
    previous = glyph;
    havePrevious = true;
    at = point.next;
  }
  return static_cast<float>(units) * size / 1000.0f;
}

std::string PdfFont::encode(std::string_view text) {
  // `[ <glyphs> adjustment <glyphs> ... ]`. A number in a `TJ` array is
  // *subtracted* from the position, in thousandths of the text space unit --
  // which is the same unit `SfntFont::kerning` answers in, negated. So a
  // tightening of -20 em units is written as 20.
  std::string out;
  out.reserve(text.size() * 4 + 4);
  out += "[<";
  std::size_t at = 0;
  std::uint16_t previous = 0;
  bool havePrevious = false;
  while(at < text.size()) {
    const util::CodePoint point = util::decodeAt(text, at);
    const std::uint16_t glyph = sfnt_.glyph(point.value);
    // `.notdef` is recorded like any other: it is a glyph the page shows, and
    // leaving it out of the widths array would have the viewer guess its
    // advance and so shift everything after it on the line.
    glyphs_.emplace(glyph, point.value);
    const int kern = havePrevious ? sfnt_.kerning(previous, glyph) : 0;
    if(kern != 0) {
      out += ">";
      out += std::to_string(-kern);
      out += "<";
    }
    appendHex16(out, glyph);
    previous = glyph;
    havePrevious = true;
    at = point.next;
  }
  out += ">]";
  return out;
}

float PdfFont::ascent(float size) const {
  return static_cast<float>(sfnt_.ascent()) * size / 1000.0f;
}

float PdfFont::descent(float size) const {
  return static_cast<float>(sfnt_.descent()) * size / 1000.0f;
}

// `/W` in the compact form the format allows: runs of consecutive glyph
// numbers share one array. A note's text is mostly one alphabet, whose glyphs
// are contiguous in every font, so this is a handful of runs rather than one
// entry per glyph.
std::string PdfFont::widthsArray() const {
  std::string out = "[";
  auto it = glyphs_.begin();
  while(it != glyphs_.end()) {
    auto run = it;
    std::uint16_t expected = it->first;
    while(run != glyphs_.end() && run->first == expected) {
      ++run;
      ++expected;
    }
    out += " ";
    out += std::to_string(it->first);
    out += " [";
    for(auto at = it; at != run; ++at) {
      out += " ";
      out += std::to_string(sfnt_.advance(at->first));
    }
    out += " ]";
    it = run;
  }
  out += " ]";
  return out;
}

std::string PdfFont::toUnicodeCMap() const {
  std::string body;
  body +=
    "/CIDInit /ProcSet findresource begin\n"
    "12 dict begin\nbegincmap\n"
    "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
    "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
    "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";

  // The format caps a `bfchar` block at 100 entries, so the pairs are written
  // in hundreds. A reader is entitled to reject a longer block, and a note
  // with two hundred distinct characters in it is an ordinary note.
  std::vector<std::pair<std::uint16_t, char32_t>> pairs(glyphs_.begin(), glyphs_.end());
  for(std::size_t at = 0; at < pairs.size(); at += 100) {
    const std::size_t take = std::min<std::size_t>(100, pairs.size() - at);
    body += std::to_string(take);
    body += " beginbfchar\n";
    for(std::size_t i = 0; i < take; ++i) {
      body += "<";
      appendHex16(body, pairs[at + i].first);
      body += "> <";
      appendUtf16Hex(body, pairs[at + i].second);
      body += ">\n";
    }
    body += "endbfchar\n";
  }
  body += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
  return body;
}

ObjectId PdfFont::write(PdfWriter& writer) {
  const ObjectId type0 = writer.reserve();
  const ObjectId descendant = writer.reserve();
  const ObjectId descriptor = writer.reserve();
  const ObjectId file = writer.reserve();
  const ObjectId toUnicode = writer.reserve();

  // Which of the two CID font shapes the file goes into is decided by what its
  // outlines are, and nothing else: `glyf` outlines are a CIDFontType2 with a
  // `/FontFile2`, CFF outlines a CIDFontType0 with a `/FontFile3`. Putting an
  // OTF in the first shape produces a file that opens and shows nothing.
  const bool cff = sfnt_.isCff();

  std::string type0Body = "/Type /Font /Subtype /Type0 ";
  type0Body += "/BaseFont " + name(baseName_) + " /Encoding /Identity-H ";
  type0Body += "/DescendantFonts [ " + std::to_string(descendant) + " 0 R ] ";
  type0Body += "/ToUnicode " + std::to_string(toUnicode) + " 0 R";
  writer.object(type0, type0Body);

  std::string descendantBody = "/Type /Font /Subtype ";
  descendantBody += cff ? "/CIDFontType0 " : "/CIDFontType2 ";
  descendantBody += "/BaseFont " + name(baseName_) + " ";
  descendantBody += "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> ";
  descendantBody += "/FontDescriptor " + std::to_string(descriptor) + " 0 R ";
  // The default width, for any glyph the array below leaves out. There are
  // none, but the key is required and a viewer that fell back to its own
  // default would space text differently from how it was measured.
  descendantBody += "/DW 1000 /W " + widthsArray();
  // Only a CIDFontType2 has one, and `/Identity` is what says the CIDs in the
  // content stream are already glyph numbers.
  if(!cff) descendantBody += " /CIDToGIDMap /Identity";
  writer.object(descendant, descendantBody);

  // Symbolic, because the font is embedded and the encoding is by glyph
  // number: the flag tells a viewer not to second-guess the mapping against a
  // standard encoding it does not have.
  int flags = 1 << 2;
  if(sfnt_.fixedPitch()) flags |= 1 << 0;
  if(sfnt_.italicAngle() != 0) flags |= 1 << 6;

  std::string descriptorBody = "/Type /FontDescriptor /FontName " + name(baseName_) + " ";
  descriptorBody += "/Flags " + std::to_string(flags) + " ";
  descriptorBody += "/FontBBox [ " + std::to_string(sfnt_.bboxMinX()) + " " +
                    std::to_string(sfnt_.bboxMinY()) + " " + std::to_string(sfnt_.bboxMaxX()) +
                    " " + std::to_string(sfnt_.bboxMaxY()) + " ] ";
  descriptorBody += "/ItalicAngle " + std::to_string(sfnt_.italicAngle()) + " ";
  descriptorBody += "/Ascent " + std::to_string(sfnt_.ascent()) + " ";
  descriptorBody += "/Descent " + std::to_string(sfnt_.descent()) + " ";
  descriptorBody += "/CapHeight " + std::to_string(sfnt_.capHeight()) + " ";
  descriptorBody += "/StemV " + std::to_string(sfnt_.stemV()) + " ";
  descriptorBody += cff ? "/FontFile3 " : "/FontFile2 ";
  descriptorBody += std::to_string(file) + " 0 R";
  writer.object(descriptor, descriptorBody);

  // Only the glyphs the pages showed, and only the tables a PDF reads. A
  // whole face is 610 KB for a note that may use sixty characters; see
  // `SfntSubset.h` for what is cut and what deliberately is not. An empty
  // answer means the file was not a shape the subsetter understands, and the
  // whole of it goes in -- a big file rather than a page of blank boxes.
  std::vector<std::uint16_t> shown;
  shown.reserve(glyphs_.size());
  for(const auto& [glyph, codePoint] : glyphs_) shown.push_back(glyph);
  std::string program = subsetFont(sfnt_.bytes(), shown);
  if(program.empty()) program = sfnt_.bytes();

  std::string fileDict;
  if(cff) {
    // `/OpenType` rather than `/CIDFontType0C`: the stream is an OpenType
    // file, not a bare CFF table lifted out of one. Which is also why the
    // subset keeps the tables OpenType calls required even where a viewer
    // would not read them.
    fileDict = "/Subtype /OpenType";
  } else {
    // A TrueType font program has to declare its own uncompressed length,
    // which is the one number a reader cannot recover from a compressed
    // stream.
    fileDict = "/Length1 " + std::to_string(program.size());
  }
  writer.stream(file, fileDict, program, true);
  writer.stream(toUnicode, "", toUnicodeCMap(), true);
  return type0;
}

}
