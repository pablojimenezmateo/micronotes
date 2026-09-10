#include "TestSupport.h"

#include "CoreAliases.h"

#include "core/pdf/Deflate.h"
#include "core/pdf/PdfFont.h"
#include "core/pdf/PdfWriter.h"
#include "core/pdf/Sfnt.h"
#include "core/pdf/SfntSubset.h"
#include "ui/Fonts.h"

#include <cstdlib>
#include <string_view>
#include <vector>
#include <utility>
#include <string>

#if MICROCORE_HAS_ZLIB
#include <zlib.h>
#endif

using micronotes::pdf::PdfFont;
using micronotes::pdf::SfntFont;

namespace {

// Byte offset of the nth object header, as the xref table claims it.
std::size_t offsetInXref(const std::string& file, int index) {
  // The leading newline matters: `startxref` ends in `xref` too, and it is the
  // last thing in the file.
  const auto xrefAt = file.rfind("\nxref\n") + 1;
  micronotes::tests::require(xrefAt != std::string::npos, "the file has no xref table");
  // Twenty bytes per entry, and entry zero is the free-list head.
  const std::size_t entry = xrefAt + 5;
  const std::size_t line = file.find('\n', entry) + 1 + static_cast<std::size_t>(index) * 20;
  return static_cast<std::size_t>(std::strtoul(file.substr(line, 10).c_str(), nullptr, 10));
}

// A table's bytes in a font file, or empty. Written out here rather than
// reached for in the subsetter, so a test that says "GPOS is gone" is reading
// the file rather than trusting the code that made it.
std::string_view tableIn(std::string_view font, const char* tag) {
  const auto be16 = [font](std::size_t at) {
    return (static_cast<unsigned>(static_cast<unsigned char>(font[at])) << 8) |
           static_cast<unsigned>(static_cast<unsigned char>(font[at + 1]));
  };
  const auto be32 = [font](std::size_t at) {
    unsigned value = 0;
    for(std::size_t i = 0; i < 4; ++i) {
      value = (value << 8) | static_cast<unsigned char>(font[at + i]);
    }
    return value;
  };
  if(font.size() < 12) return {};
  for(unsigned i = 0; i < be16(4); ++i) {
    const std::size_t record = 12 + i * 16;
    if(record + 16 > font.size()) break;
    if(font.compare(record, 4, tag) != 0) continue;
    const unsigned offset = be32(record + 8);
    const unsigned length = be32(record + 12);
    if(offset + length > font.size()) return {};
    return font.substr(offset, length);
  }
  return {};
}

// A glyph's outline bytes, read off `loca`. Empty means the glyph was dropped.
//
// `longFormat` because the original faces here use the short form -- halved
// offsets -- and every subset uses the long one, so a reader of both has to
// be told which.
std::string_view outlineOf(std::string_view font, std::uint16_t glyph, bool longFormat) {
  const std::string_view loca = tableIn(font, "loca");
  const std::string_view glyf = tableIn(font, "glyf");
  const auto offset = [loca, longFormat](std::size_t index) -> std::size_t {
    if(!longFormat) {
      const std::size_t at = index * 2;
      if(at + 2 > loca.size()) return 0;
      return ((static_cast<std::size_t>(static_cast<unsigned char>(loca[at])) << 8) |
              static_cast<unsigned char>(loca[at + 1])) *
             2;
    }
    const std::size_t at = index * 4;
    if(at + 4 > loca.size()) return 0;
    std::size_t value = 0;
    for(std::size_t i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(loca[at + i]);
    return value;
  };
  const std::size_t from = offset(glyph);
  const std::size_t to = offset(static_cast<std::size_t>(glyph) + 1);
  if(to <= from || to > glyf.size()) return {};
  return glyf.substr(from, to - from);
}

unsigned outlineBytes(std::string_view font, std::uint16_t glyph) {
  return static_cast<unsigned>(outlineOf(font, glyph, true).size());
}

SfntFont faceFor(micronotes::ui::FontFamily family) {
  auto font = SfntFont::open(micronotes::ui::faceFile(family, false, false));
  micronotes::tests::require(font.has_value(), "a vendored face did not parse");
  return std::move(*font);
}

// A string's width with every pair adjustment dropped, which is what the
// exporter measured before TD-38 was paid.
float unkernedWidth(const SfntFont& font, std::string_view text, float size) {
  int units = 0;
  for(const char c : text) units += font.advance(font.glyph(static_cast<unsigned char>(c)));
  return static_cast<float>(units) * size / 1000.0f;
}

}

// The number formatter is the piece of a PDF writer that fails silently and
// catastrophically: `std::to_string` on a double is locale-dependent, so on a
// machine set to a comma decimal separator every coordinate in every content
// stream becomes two operands and no reader opens the file.
MICRONOTES_TEST(pdf_numbers_are_plain_decimal_and_trimmed) {
  using micronotes::pdf::number;
  MICRONOTES_REQUIRE(number(0.0) == "0");
  MICRONOTES_REQUIRE(number(12.0) == "12");
  MICRONOTES_REQUIRE(number(12.5) == "12.5");
  MICRONOTES_REQUIRE(number(-3.25) == "-3.25");
  // Rounded to a hundredth of a point, not carried to seventeen digits.
  MICRONOTES_REQUIRE(number(1.0 / 3.0) == "0.33");
  // Never a comma, whatever the machine is set to.
  MICRONOTES_REQUIRE(number(841.89).find(',') == std::string::npos);
  // `-0` is a number no reader minds and every diff does.
  MICRONOTES_REQUIRE(number(-0.001) == "0");
}

MICRONOTES_TEST(pdf_names_and_strings_escape_what_would_break_the_syntax) {
  using micronotes::pdf::literalString;
  using micronotes::pdf::name;
  MICRONOTES_REQUIRE(literalString("a (b) c") == "(a \\(b\\) c)");
  MICRONOTES_REQUIRE(literalString("back\\slash") == "(back\\\\slash)");
  MICRONOTES_REQUIRE(name("Inter-Regular") == "/Inter-Regular");
  MICRONOTES_REQUIRE(name("a b") == "/a#20b");
}

// The xref table is the one part of the format nothing else checks: a file
// with wrong offsets parses as far as the trailer and then fails to find a
// single object.
MICRONOTES_TEST(pdf_cross_references_point_at_their_own_objects) {
  micronotes::pdf::PdfWriter writer;
  const auto first = writer.reserve();
  const auto second = writer.reserve();
  const auto third = writer.reserve();
  writer.object(first, "/Type /Catalog /Pages " + std::to_string(second) + " 0 R");
  writer.stream(second, "/Type /Something", "a stream body", false);
  writer.object(third, "/Type /Last");
  const std::string file = writer.finish(first, 0);

  MICRONOTES_REQUIRE(file.rfind("%PDF-1.7", 0) == 0);
  MICRONOTES_REQUIRE(file.find("%%EOF") != std::string::npos);
  for(int id = 1; id <= 3; ++id) {
    const std::size_t at = offsetInXref(file, id);
    const std::string header = std::to_string(id) + " 0 obj";
    MICRONOTES_REQUIRE(file.compare(at, header.size(), header) == 0);
  }
  // And `startxref` points at the table itself.
  const auto startxref = file.rfind("startxref\n");
  const auto claimed =
    static_cast<std::size_t>(std::strtoul(file.c_str() + startxref + 10, nullptr, 10));
  MICRONOTES_REQUIRE(file.compare(claimed, 4, "xref") == 0);
}

// A dictionary object writes its own braces, so a caller cannot leave them off
// -- which is the mistake that produced a file whose catalog was not a
// dictionary at all and which every reader rejected without saying why.
MICRONOTES_TEST(pdf_objects_are_dictionaries_without_the_caller_saying_so) {
  micronotes::pdf::PdfWriter writer;
  const auto id = writer.reserve();
  writer.object(id, "/Type /Catalog");
  const std::string file = writer.finish(id, 0);
  MICRONOTES_REQUIRE(file.find("<< /Type /Catalog >>") != std::string::npos);
}

// The fallback path has to produce a real zlib stream, not an approximation of
// one: a PDF built on a machine with no zlib must open on a machine that has
// it, and `FlateDecode` is not optional in the file format.
MICRONOTES_TEST(pdf_deflate_produces_a_zlib_stream) {
  const std::string data(4096, 'x');
  const std::string packed = micronotes::pdf::deflate(data);
  MICRONOTES_REQUIRE(packed.size() > 2);
  MICRONOTES_REQUIRE(static_cast<unsigned char>(packed[0]) == 0x78);
  // The two header bytes are a multiple of 31, which is the check a reader
  // applies before it decodes anything.
  const unsigned header = (static_cast<unsigned char>(packed[0]) << 8) |
                          static_cast<unsigned char>(packed[1]);
  MICRONOTES_REQUIRE(header % 31 == 0);
#if MICROCORE_HAS_ZLIB
  std::string out(data.size() + 64, '\0');
  uLongf size = static_cast<uLongf>(out.size());
  const int result = uncompress(reinterpret_cast<Bytef*>(out.data()), &size,
                                reinterpret_cast<const Bytef*>(packed.data()),
                                static_cast<uLong>(packed.size()));
  MICRONOTES_REQUIRE(result == Z_OK);
  MICRONOTES_REQUIRE(std::string(out.data(), size) == data);
#endif
}

// The font reader is what makes the exported line breaks the same line breaks
// the reader's viewer will draw. If it reports the wrong advances the text
// overruns the column it was measured into, and nothing else notices.
MICRONOTES_TEST(sfnt_reads_the_vendored_sans_face) {
  const auto path = micronotes::ui::faceFile(micronotes::ui::FontFamily::Sans, false, false);
  micronotes::tests::require(!path.empty(), "no sans face resolved");
  const auto font = SfntFont::open(path);
  micronotes::tests::require(font.has_value(), "the sans face did not parse");

  const std::uint16_t a = font->glyph(U'A');
  MICRONOTES_REQUIRE(a != 0);
  MICRONOTES_REQUIRE(font->glyphCount() > 100);
  // Advances are reported in the 1000-unit em a PDF measures in, whatever the
  // font's own units are. A capital A is between a third and a whole em in
  // every text face there is; a value outside that means the scaling is wrong.
  const int advance = font->advance(a);
  MICRONOTES_REQUIRE(advance > 300 && advance < 1000);
  // A space is narrower than an A, and both are narrower than an em.
  MICRONOTES_REQUIRE(font->advance(font->glyph(U' ')) < advance);
  MICRONOTES_REQUIRE(font->ascent() > 0);
  MICRONOTES_REQUIRE(font->descent() < 0);
  MICRONOTES_REQUIRE(!font->bytes().empty());
}

// Which of the two PDF font shapes a face goes into is decided by this, and
// putting an OTF in the wrong one produces a file that opens and shows blank
// pages -- which is why it is asserted rather than assumed.
MICRONOTES_TEST(sfnt_tells_cff_outlines_from_truetype_ones) {
  const auto sans = SfntFont::open(
    micronotes::ui::faceFile(micronotes::ui::FontFamily::Sans, false, false));
  const auto mono = SfntFont::open(
    micronotes::ui::faceFile(micronotes::ui::FontFamily::Mono, false, false));
  micronotes::tests::require(sans.has_value() && mono.has_value(), "a vendored face is missing");
  // Inter is shipped as OpenType/CFF and JetBrains Mono as TrueType.
  MICRONOTES_REQUIRE(sans->isCff());
  MICRONOTES_REQUIRE(!mono->isCff());
}

// A code point the font has no glyph for is `.notdef`, drawn as an empty box.
// Deliberately not an error: a note with an emoji in it exports with a box
// where the emoji was rather than failing to export.
MICRONOTES_TEST(sfnt_answers_notdef_rather_than_failing) {
  const auto font = SfntFont::open(
    micronotes::ui::faceFile(micronotes::ui::FontFamily::Sans, false, false));
  micronotes::tests::require(font.has_value(), "the sans face did not parse");
  MICRONOTES_REQUIRE(font->glyph(0x10FFFD) == 0);
}

// The pair kerning the vendored sans face carries is read, which is the whole
// premise of TD-38: exported text used to be set at the faces' default pair
// spacing, so `AV` and `To` sat visibly loose -- invisible in a paragraph and
// obvious in a 30pt title.
//
// Read out of `GPOS` rather than a `kern` table, and that is not a detail:
// neither vendored face has a `kern` table at all, so a reader of that table
// would have kerned nothing while looking like it worked. Negative numbers,
// because kerning is very nearly always a tightening.
MICRONOTES_TEST(sfnt_reads_the_pair_kerning_in_gpos) {
  const SfntFont sans = faceFor(micronotes::ui::FontFamily::Sans);
  MICRONOTES_REQUIRE(sans.kerns());
  const auto pair = [&sans](char left, char right) {
    return sans.kerning(sans.glyph(static_cast<char32_t>(left)),
                        sans.glyph(static_cast<char32_t>(right)));
  };
  MICRONOTES_REQUIRE(pair('A', 'V') < 0);
  MICRONOTES_REQUIRE(pair('T', 'o') < 0);
  MICRONOTES_REQUIRE(pair('L', 'T') < 0);
  // A pair no face kerns, so a reader that returned its last answer for
  // everything would fail here rather than passing three times over.
  MICRONOTES_REQUIRE(pair('n', 'o') == 0);
  // And nothing before the first glyph of a run: an adjustment charged there
  // would move the run's own origin away from where the layout placed it.
  MICRONOTES_REQUIRE(sans.kerning(0, sans.glyph('V')) == 0);
}

// The monospace face is not kerned, and it must not be: a code block is a
// grid, and a pair adjustment inside one puts the characters off it.
//
// This holds because JetBrains Mono declares no `kern` feature rather than
// because anything here suppresses it -- which is worth a test, since the day
// a mono face ships with one is the day a code block stops lining up.
MICRONOTES_TEST(sfnt_leaves_the_monospace_face_unkerned) {
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  MICRONOTES_REQUIRE(!mono.kerns());
  int pairs = 0;
  for(int left = 32; left < 127; ++left) {
    for(int right = 32; right < 127; ++right) {
      if(mono.kerning(mono.glyph(static_cast<char32_t>(left)),
                      mono.glyph(static_cast<char32_t>(right))) != 0) {
        ++pairs;
      }
    }
  }
  MICRONOTES_REQUIRE(pairs == 0);
}

// The measurement includes the kerning, which is the half of TD-38 that
// matters most: the layout breaks its lines with this number. Measuring
// unkerned and drawing kerned would put the text back out of step with its own
// line breaks, which is the failure `PdfFont` exists to make impossible.
MICRONOTES_TEST(pdf_font_measures_a_kerned_pair_tighter_than_its_glyphs) {
  const SfntFont sans = faceFor(micronotes::ui::FontFamily::Sans);
  PdfFont font(sans, "Test");
  const float size = 30.0f;
  MICRONOTES_REQUIRE(font.width("AV", size) < unkernedWidth(sans, "AV", size));
  MICRONOTES_REQUIRE(font.width("To", size) < unkernedWidth(sans, "To", size));
  // A word with no kerned pair in it measures exactly as its glyphs do.
  const float plain = unkernedWidth(sans, "monsoon", size);
  const float kerned = font.width("monsoon", size);
  MICRONOTES_REQUIRE(kerned > plain - 0.01f && kerned < plain + 0.01f);
}

// And the output includes it, as the numbers of a `TJ` array -- which is the
// only way a PDF can say "these two glyphs sit closer than their advances".
//
// The sign is the part worth pinning: a number in a `TJ` array is *subtracted*
// from the position, so a tightening is written positive. Getting it backwards
// sets `AV` looser than the default rather than tighter, which looks like a
// font problem rather than a sign error.
MICRONOTES_TEST(pdf_font_writes_the_kerning_into_the_tj_array) {
  const SfntFont sans = faceFor(micronotes::ui::FontFamily::Sans);
  PdfFont font(sans, "Test");
  const std::string kerned = font.encode("AV");
  // `[<glyph>NN<glyph>]`, so the array has a number between two hex strings.
  MICRONOTES_REQUIRE(kerned.front() == '[' && kerned.back() == ']');
  const auto gap = kerned.find(">");
  MICRONOTES_REQUIRE(gap != std::string::npos && gap + 1 < kerned.size());
  MICRONOTES_REQUIRE(kerned[gap + 1] >= '1' && kerned[gap + 1] <= '9');

  // An unkerned run is one hex string and no numbers at all.
  const std::string plain = font.encode("no");
  MICRONOTES_REQUIRE(plain.find('>') == plain.size() - 2);
}

// The two halves agree to the unit: what the measurement adds up to is what
// the content stream tells the viewer to draw.
//
// Summed off the array's own numbers rather than recomputed from the face, so
// a change that kerned the measure and not the encoding -- or the other way
// round -- fails here rather than showing up as text that overruns the line
// breaks computed for it.
MICRONOTES_TEST(pdf_font_measures_exactly_what_it_encodes) {
  const SfntFont sans = faceFor(micronotes::ui::FontFamily::Sans);
  PdfFont font(sans, "Test");
  const std::string_view text = "AVATAR To Ty WAVE, fifty.";
  const float size = 24.0f;
  const std::string array = font.encode(text);

  // Every number between two hex strings, which is the kerning the stream
  // applies -- negated, because a `TJ` number moves the position back.
  int applied = 0;
  for(std::size_t at = 0; at < array.size(); ++at) {
    if(array[at] != '>') continue;
    const std::size_t number = at + 1;
    if(number >= array.size() || array[number] == ']') break;
    applied -= std::atoi(array.c_str() + number);
    at = array.find('<', number);
    if(at == std::string::npos) break;
  }
  MICRONOTES_REQUIRE(applied < 0);

  const float expected =
    unkernedWidth(sans, text, size) + static_cast<float>(applied) * size / 1000.0f;
  const float measured = font.width(text, size);
  MICRONOTES_REQUIRE(measured > expected - 0.01f && measured < expected + 0.01f);
}

// A subset keeps the glyph numbers it was handed, which is the single fact
// that makes subsetting here a filter rather than a font toolchain.
//
// The exporter writes text as `Identity-H` with an `/Identity` CID-to-GID map,
// so a glyph's number in the content stream *is* its number in the font
// program. Renumbering would mean rewriting every composite glyph's component
// references, the charset and the widths array -- which is what
// `docs/tech-debt.md` said the work was, before anyone noticed it did not have
// to be done at all.
MICRONOTES_TEST(sfnt_subset_keeps_the_glyph_numbers_it_was_given) {
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  const std::uint16_t kept = mono.glyph(U'k');
  const std::string subset = micronotes::pdf::subsetFont(mono.bytes(), {kept});
  MICRONOTES_REQUIRE(!subset.empty());
  MICRONOTES_REQUIRE(subset.size() < mono.bytes().size() / 4);

  const auto reparsed = SfntFont::fromBytes(subset);
  micronotes::tests::require(reparsed.has_value(), "the subset is not a font any more");
  MICRONOTES_REQUIRE(reparsed->glyphCount() == mono.glyphCount());
  // The same glyph number, and the same advance, so every width already
  // written into the file still describes the glyph it names.
  MICRONOTES_REQUIRE(reparsed->advance(kept) == mono.advance(kept));
  MICRONOTES_REQUIRE(reparsed->advance(mono.glyph(U'z')) == mono.advance(mono.glyph(U'z')));
  // The one the note used has its outline; the one it did not has none.
  MICRONOTES_REQUIRE(outlineBytes(subset, kept) > 0);
  MICRONOTES_REQUIRE(outlineBytes(subset, mono.glyph(U'z')) == 0);
}

// A composite glyph brings its components with it, transitively.
//
// `é` is drawn as `e` with an acute over it -- a composite whose component
// references are glyph numbers into the same font. Dropping `e` because the
// note never used it on its own leaves an accented letter that draws the
// accent and nothing under it, and only for the letters that happen to be
// composites: exactly the kind of gap that reaches a reader rather than a
// test.
MICRONOTES_TEST(sfnt_subset_keeps_a_composite_glyphs_components) {
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  const std::uint16_t accented = mono.glyph(U'\u00e9');
  const std::uint16_t plain = mono.glyph(U'e');
  micronotes::tests::require(accented != 0 && plain != 0 && accented != plain,
                             "the mono face does not have the pair this is about");
  // Only the accented letter asked for.
  const std::string subset = micronotes::pdf::subsetFont(mono.bytes(), {accented});
  MICRONOTES_REQUIRE(!subset.empty());
  MICRONOTES_REQUIRE(outlineBytes(subset, accented) > 0);
  MICRONOTES_REQUIRE(outlineBytes(subset, plain) > 0);
}

// The tables a PDF has no use for are gone.
//
// `GPOS` and `GSUB` are the shaper's, and there is no shaper on the other side
// of a PDF: the pair kerning `GPOS` carries is already resolved into the `TJ`
// arrays of the content stream. `cmap` turns a character into a glyph, and the
// CID-to-GID map is `/Identity`, so nothing ever looks a character up.
MICRONOTES_TEST(sfnt_subset_drops_the_tables_a_pdf_does_not_read) {
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  const std::string subset = micronotes::pdf::subsetFont(mono.bytes(), {mono.glyph(U'k')});
  MICRONOTES_REQUIRE(tableIn(subset, "GPOS").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "GSUB").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "cmap").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "post").empty());
  // And the ones it does are still there.
  MICRONOTES_REQUIRE(!tableIn(subset, "glyf").empty());
  MICRONOTES_REQUIRE(!tableIn(subset, "loca").empty());
  MICRONOTES_REQUIRE(!tableIn(subset, "hmtx").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "head").size() >= 54);
}

// A CFF face keeps every table OpenType calls required, because the stream is
// declared `/Subtype /OpenType` and so has to stay a valid OpenType file. What
// it does not keep is the 26 KB of character map inside them: that becomes the
// format's own "nothing is mapped" subtable.
MICRONOTES_TEST(sfnt_subset_keeps_a_cff_face_a_valid_opentype_file) {
  const SfntFont sans = faceFor(micronotes::ui::FontFamily::Sans);
  micronotes::tests::require(sans.isCff(), "the sans face is not the CFF one this is about");
  const std::string subset = micronotes::pdf::subsetFont(sans.bytes(), {sans.glyph(U'k')});
  MICRONOTES_REQUIRE(!subset.empty());
  for(const char* tag : {"CFF ", "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post"}) {
    micronotes::tests::require(!tableIn(subset, tag).empty(),
                               std::string("a subsetted CFF face lost its ") + tag);
  }
  MICRONOTES_REQUIRE(tableIn(subset, "GPOS").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "GSUB").empty());
  MICRONOTES_REQUIRE(tableIn(subset, "cmap").size() < 64);
  // The CFF table keeps its byte range -- that is what lets the charstrings be
  // cut without re-laying out the Top DICT's offsets -- so the *file* is not
  // much smaller. What shrinks is the compressed stream, which is what a note
  // is measured in.
  const auto reparsed = SfntFont::fromBytes(subset);
  micronotes::tests::require(reparsed.has_value(), "the subsetted CFF face is not a font");
  MICRONOTES_REQUIRE(reparsed->isCff());
  MICRONOTES_REQUIRE(reparsed->glyphCount() == sans.glyphCount());
  MICRONOTES_REQUIRE(reparsed->advance(sans.glyph(U'k')) == sans.advance(sans.glyph(U'k')));
}

// Bytes it does not understand produce nothing, which is what tells the caller
// to embed the whole file. A font that cannot be cut down is a big file; a
// font cut down wrongly is a page of blank boxes.
MICRONOTES_TEST(sfnt_subset_gives_up_rather_than_guessing) {
  MICRONOTES_REQUIRE(micronotes::pdf::subsetFont("not a font at all", {1}).empty());
  MICRONOTES_REQUIRE(micronotes::pdf::subsetFont("", {1}).empty());
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  // Truncated to its table directory: every table it names is out of range.
  MICRONOTES_REQUIRE(micronotes::pdf::subsetFont(mono.bytes().substr(0, 200), {1}).empty());
}

// A font file is data from disk, so every read in the sfnt code is bounds
// checked against the *buffer* rather than against the table that claimed to
// contain it. This is the test that says so: the real faces, cut off at two
// hundred lengths and mutated at two hundred offsets, through the parser, the
// kerning lookup and the subsetter.
//
// Nothing here asserts a result -- a mangled font may parse or may not, and
// either is a correct answer. What it asserts is that the code returns at all,
// which under a sanitizer build is the whole point: a read past the end of the
// buffer is a crash here rather than an export that works on this machine and
// not on the next one.
// A kept glyph's outline comes through byte for byte.
//
// This is the claim the whole subsetter rests on and the one a size assertion
// cannot make: a subset that dropped a byte of a curve is a page that renders,
// passes every structural check, and shows the wrong shape. Proved against the
// original's own bytes rather than against a stored expectation, so it holds
// whichever face is vendored.
//
// The external half of the same claim is a `cmp` of the rasterised page
// against an export with the fonts embedded whole -- `gs` and `pdftoppm` both
// give the same pixels for both -- which is not in the suite because neither
// tool is a build dependency.
MICRONOTES_TEST(sfnt_subset_copies_a_kept_glyphs_outline_exactly) {
  const SfntFont mono = faceFor(micronotes::ui::FontFamily::Mono);
  const bool longFormat = tableIn(mono.bytes(), "head")[50] == 0 &&
                          tableIn(mono.bytes(), "head")[51] == 1;
  std::vector<std::uint16_t> wanted;
  for(const char32_t point : {U'k', U'W', U'0', U'{', U'-'}) wanted.push_back(mono.glyph(point));
  const std::string subset = micronotes::pdf::subsetFont(mono.bytes(), wanted);
  MICRONOTES_REQUIRE(!subset.empty());

  int compared = 0;
  for(const std::uint16_t glyph : wanted) {
    const std::string_view original = outlineOf(mono.bytes(), glyph, longFormat);
    const std::string_view kept = outlineOf(subset, glyph, true);
    micronotes::tests::require(!original.empty(), "the face has no outline for a glyph asked for");
    // The subset pads each glyph up to a four-byte boundary, so the kept range
    // may be longer -- but it has to *start* with the original, exactly.
    micronotes::tests::require(kept.size() >= original.size(),
                               "a kept glyph lost bytes of its outline");
    micronotes::tests::require(kept.substr(0, original.size()) == original,
                               "a kept glyph's outline is not the one it came from");
    ++compared;
  }
  MICRONOTES_REQUIRE(compared == 5);
}

MICRONOTES_TEST(sfnt_survives_a_font_file_that_has_been_damaged) {
  for(const auto family : {micronotes::ui::FontFamily::Sans, micronotes::ui::FontFamily::Mono}) {
    const std::string whole = faceFor(family).bytes();
    const auto exercise = [](const std::string& bytes) {
      const auto font = SfntFont::fromBytes(bytes);
      if(!font) return;
      // Enough of the glyph space to reach every lookup shape, and the pairs
      // between them.
      std::vector<std::uint16_t> glyphs;
      std::uint16_t previous = 0;
      for(char32_t point = 0x20; point < 0x180; point += 7) {
        const std::uint16_t glyph = font->glyph(point);
        glyphs.push_back(glyph);
        (void)font->advance(glyph);
        (void)font->kerning(previous, glyph);
        previous = glyph;
      }
      const std::string subset = micronotes::pdf::subsetFont(font->bytes(), glyphs);
      // A subset that came back has to be a font: handing a viewer something
      // that is not is the failure mode worth the check.
      if(!subset.empty()) (void)SfntFont::fromBytes(subset);
    };

    for(int i = 0; i < 200; ++i) {
      // Spread over the whole file rather than bunched at one end, so the cut
      // lands inside a table directory, inside `cmap`, inside `GPOS` and
      // inside the outlines in turn.
      exercise(whole.substr(0, whole.size() * static_cast<std::size_t>(i) / 200));
    }
    for(int i = 0; i < 200; ++i) {
      std::string damaged = whole;
      const std::size_t at = damaged.size() * static_cast<std::size_t>(i) / 200;
      // A byte of 0xFF is the interesting one: in an offset or a count it is
      // the largest value the field can hold, which is what turns a length
      // into a read past the end.
      for(std::size_t byte = at; byte < std::min(at + 4, damaged.size()); ++byte) {
        damaged[byte] = static_cast<char>(0xFF);
      }
      exercise(damaged);
    }
  }
}

MICRONOTES_TEST(sfnt_refuses_bytes_that_are_not_a_font) {
  MICRONOTES_REQUIRE(!SfntFont::fromBytes("not a font at all").has_value());
  MICRONOTES_REQUIRE(!SfntFont::fromBytes("").has_value());
  // A truncated header must be refused rather than read past.
  MICRONOTES_REQUIRE(!SfntFont::fromBytes(std::string("\x00\x01\x00\x00\x00", 5)).has_value());
}
