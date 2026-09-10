#include "core/pdf/Sfnt.h"

#include "core/pdf/SfntRead.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

namespace microcore::pdf {

using sfnt::s16At;
using sfnt::u16At;
using sfnt::u32At;
using sfnt::u8At;

namespace {

// A weight class turned into the stem width a `/FontDescriptor` asks for.
//
// There is no stem width in an sfnt file. Every producer estimates it, and the
// estimate only has to be in the right neighbourhood: a viewer uses it to
// synthesise a substitute face when the embedded one cannot be used, which for
// an embedded font is a path nothing takes. The curve below is the usual one --
// roughly 10 units at Thin, 80 at Regular, 165 at Black.
int stemVFor(std::uint16_t weightClass) {
  const int weight = weightClass == 0 ? 400 : static_cast<int>(weightClass);
  return 10 + (weight * weight) / 2000;
}

}

std::optional<SfntFont> SfntFont::open(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if(!in) return std::nullopt;
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if(bytes.empty()) return std::nullopt;
  return fromBytes(std::move(bytes));
}

std::optional<SfntFont> SfntFont::fromBytes(std::string bytes) {
  SfntFont font;
  font.bytes_ = std::move(bytes);
  if(!font.parse()) return std::nullopt;
  return font;
}

std::string_view SfntFont::table(const char* tag) const {
  const std::string_view all(bytes_);
  const std::uint16_t count = u16At(all, 4);
  for(std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = 12 + static_cast<std::size_t>(i) * 16;
    if(record + 16 > all.size()) break;
    if(std::memcmp(all.data() + record, tag, 4) != 0) continue;
    const std::uint32_t offset = u32At(all, record + 8);
    const std::uint32_t length = u32At(all, record + 12);
    if(offset >= all.size()) return {};
    return all.substr(offset, std::min<std::size_t>(length, all.size() - offset));
  }
  return {};
}

int SfntFont::toEm(int units) const {
  if(unitsPerEm_ <= 0) return units;
  // Rounded away from zero so a negative descent does not creep toward it.
  const int scaled = units * 1000;
  return scaled >= 0 ? (scaled + unitsPerEm_ / 2) / unitsPerEm_
                     : -((-scaled + unitsPerEm_ / 2) / unitsPerEm_);
}

bool SfntFont::parse() {
  const std::string_view all(bytes_);
  if(all.size() < 12) return false;
  const std::uint32_t version = u32At(all, 0);
  // `OTTO` is OpenType with CFF outlines; 0x00010000 and `true` are TrueType.
  // A collection (`ttcf`) is deliberately not handled: nothing vendored here
  // is one, and picking a face out of a collection is a different job.
  isCff_ = version == 0x4F54544F;
  if(!isCff_ && version != 0x00010000 && version != 0x74727565) return false;

  const std::string_view head = table("head");
  if(head.size() < 54) return false;
  unitsPerEm_ = u16At(head, 18);
  if(unitsPerEm_ <= 0) unitsPerEm_ = 1000;
  bboxMinX_ = toEm(s16At(head, 36));
  bboxMinY_ = toEm(s16At(head, 38));
  bboxMaxX_ = toEm(s16At(head, 40));
  bboxMaxY_ = toEm(s16At(head, 42));

  const std::string_view maxp = table("maxp");
  glyphCount_ = maxp.size() >= 6 ? u16At(maxp, 4) : 0;

  const std::string_view hhea = table("hhea");
  if(hhea.size() >= 36) {
    ascent_ = toEm(s16At(hhea, 4));
    descent_ = toEm(s16At(hhea, 6));
    hMetricCount_ = u16At(hhea, 34);
  }

  const std::string_view hmtxTable = table("hmtx");
  // The tables are kept as offsets rather than views so that moving the font
  // out of `fromBytes` does not leave every one of them pointing at the moved-
  // from string's buffer.
  const auto record = [&](std::string_view view) {
    Table entry;
    if(view.empty()) return entry;
    entry.offset = static_cast<std::uint32_t>(view.data() - all.data());
    entry.length = static_cast<std::uint32_t>(view.size());
    return entry;
  };
  hmtx_ = record(hmtxTable);
  head_ = record(head);
  hhea_ = record(hhea);
  maxp_ = record(maxp);

  const std::string_view os2 = table("OS/2");
  os2_ = record(os2);
  if(os2.size() >= 6) stemV_ = stemVFor(u16At(os2, 4));
  // `sCapHeight` only exists from version 2 of the table. Below that, and for
  // a font that leaves it zero, the ascent is the better guess than nothing:
  // a wrong cap height changes no layout, only a substitute face nobody sees.
  if(os2.size() >= 90 && u16At(os2, 0) >= 2) {
    const int capHeight = toEm(s16At(os2, 88));
    if(capHeight > 0) capHeight_ = capHeight;
  } else {
    capHeight_ = ascent_;
  }
  // Typo metrics beat hhea's when they are there: hhea's ascent on a font with
  // tall accents is the tallest glyph rather than the line the text sits on.
  if(os2.size() >= 74) {
    const int typoAscent = toEm(s16At(os2, 68));
    const int typoDescent = toEm(s16At(os2, 70));
    if(typoAscent > 0) ascent_ = typoAscent;
    if(typoDescent < 0) descent_ = typoDescent;
  }

  const std::string_view post = table("post");
  post_ = record(post);
  if(post.size() >= 32) {
    // A 16.16 fixed-point angle; the PDF wants whole degrees.
    italicAngle_ = static_cast<int>(static_cast<std::int32_t>(u32At(post, 4)) / 65536);
    fixedPitch_ = u32At(post, 12) != 0;
  }

  const std::string_view gpos = table("GPOS");
  gpos_ = record(gpos);
  kern_.parse(gpos);

  cmap_ = record(table("cmap"));
  return parseCmap();
}

// The best Unicode subtable the file has, chosen once.
//
// Preference order is by *coverage*, not by platform: a format 12 table reaches
// past the BMP and a format 4 one does not, so a note with an emoji in it needs
// the first if the font has one. Windows tables are tried before Unicode ones
// only because every font that has both keeps them identical and the Windows
// one is the better-tested path.
bool SfntFont::parseCmap() {
  if(cmap_.length == 0) return true;
  const std::string_view cmap(bytes_.data() + cmap_.offset, cmap_.length);
  const std::uint16_t count = u16At(cmap, 2);
  int best = -1;
  for(std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = 4 + static_cast<std::size_t>(i) * 8;
    const std::uint16_t platform = u16At(cmap, record);
    const std::uint16_t encoding = u16At(cmap, record + 2);
    const std::uint32_t offset = u32At(cmap, record + 4);
    if(offset >= cmap.size()) continue;
    const int format = u16At(cmap, offset);
    if(format != 4 && format != 12) continue;
    const bool unicode = (platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0;
    if(!unicode) continue;
    // Higher is better: a format 12 table wins over a format 4 one.
    const int rank = format;
    if(rank <= best) continue;
    best = rank;
    cmapFormat_ = format;
    cmapOffset_ = cmap_.offset + offset;
  }
  return true;
}

std::uint16_t SfntFont::lookupFormat4(char32_t codepoint) const {
  if(codepoint > 0xFFFF) return 0;
  const std::string_view all(bytes_);
  const std::size_t base = cmapOffset_;
  const std::uint16_t segCount = u16At(all, base + 6) / 2;
  if(segCount == 0) return 0;
  const std::size_t endCodes = base + 14;
  const std::size_t startCodes = endCodes + segCount * 2u + 2u;
  const std::size_t idDeltas = startCodes + segCount * 2u;
  const std::size_t idRangeOffsets = idDeltas + segCount * 2u;

  // The segments are sorted by end code, so the segment covering a codepoint is
  // the first whose end code reaches it.
  std::uint16_t low = 0;
  std::uint16_t high = segCount;
  while(low < high) {
    const std::uint16_t mid = static_cast<std::uint16_t>(low + (high - low) / 2);
    if(u16At(all, endCodes + mid * 2u) < codepoint) low = static_cast<std::uint16_t>(mid + 1);
    else high = mid;
  }
  if(low >= segCount) return 0;
  const std::uint16_t start = u16At(all, startCodes + low * 2u);
  if(codepoint < start) return 0;

  const std::uint16_t rangeOffset = u16At(all, idRangeOffsets + low * 2u);
  const std::int16_t delta = s16At(all, idDeltas + low * 2u);
  if(rangeOffset == 0) {
    return static_cast<std::uint16_t>((codepoint + delta) & 0xFFFF);
  }
  // The infamous indirection: the offset is measured from the slot it was read
  // out of, into the glyph array that follows the segment tables.
  const std::size_t at = idRangeOffsets + low * 2u + rangeOffset +
                         (static_cast<std::size_t>(codepoint) - start) * 2u;
  const std::uint16_t glyph = u16At(all, at);
  if(glyph == 0) return 0;
  return static_cast<std::uint16_t>((glyph + delta) & 0xFFFF);
}

std::uint16_t SfntFont::lookupFormat12(char32_t codepoint) const {
  const std::string_view all(bytes_);
  const std::size_t base = cmapOffset_;
  const std::uint32_t groups = u32At(all, base + 12);
  const std::size_t first = base + 16;
  std::uint32_t low = 0;
  std::uint32_t high = groups;
  while(low < high) {
    const std::uint32_t mid = low + (high - low) / 2;
    const std::size_t at = first + static_cast<std::size_t>(mid) * 12u;
    if(u32At(all, at + 4) < codepoint) low = mid + 1;
    else high = mid;
  }
  if(low >= groups) return 0;
  const std::size_t at = first + static_cast<std::size_t>(low) * 12u;
  const std::uint32_t start = u32At(all, at);
  if(codepoint < start) return 0;
  return static_cast<std::uint16_t>(u32At(all, at + 8) + (codepoint - start));
}

std::uint16_t SfntFont::glyph(char32_t codepoint) const {
  if(cmapFormat_ == 4) return lookupFormat4(codepoint);
  if(cmapFormat_ == 12) return lookupFormat12(codepoint);
  return 0;
}

int SfntFont::kerning(std::uint16_t left, std::uint16_t right) const {
  if(kern_.empty() || gpos_.length == 0) return 0;
  const std::string_view gpos(bytes_.data() + gpos_.offset, gpos_.length);
  return toEm(kern_.adjustment(gpos, left, right));
}

int SfntFont::advance(std::uint16_t glyph) const {
  if(hmtx_.length == 0 || hMetricCount_ == 0) return toEm(unitsPerEm_ / 2);
  const std::string_view all(bytes_);
  // Past the last full metric the advance repeats: a monospaced tail is stored
  // once, and only the side bearings continue.
  const std::uint16_t index = std::min<std::uint16_t>(glyph, hMetricCount_ - 1);
  return toEm(u16At(all, hmtx_.offset + static_cast<std::size_t>(index) * 4u));
}

}
