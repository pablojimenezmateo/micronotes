#pragma once

#include "core/pdf/SfntKern.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// A font file read for the handful of facts a PDF needs about it.
//
// Not a rasterizer and not a shaper. It answers which glyph a codepoint is,
// how wide that glyph is, how much closer the face wants a *pair* of them, and
// the dozen numbers a PDF `/FontDescriptor` is made of -- and it hands back
// the file's own bytes, which is what gets embedded. The pair is the one thing
// here that is about two glyphs at once, and it is here rather than in a
// shaper because it is a number the file states: see `SfntKern.h` for what is
// deliberately left out of it.
//
// It exists because the exporter has to measure text with *the same* metrics
// the reader's PDF viewer will lay it out with. Measuring through the screen's
// rasterizer and then embedding a font file would be two different answers to
// one question, and the text would drift from its own line breaks -- the same
// failure `app/TabStrip.h` describes for a strip laid out twice.
namespace microcore::pdf {

class SfntFont {
public:
  static std::optional<SfntFont> open(const std::filesystem::path& path);
  static std::optional<SfntFont> fromBytes(std::string bytes);

  // The glyph a codepoint maps to, or 0 -- `.notdef`, which a viewer draws as
  // an empty box. Deliberately not an error: a note with an emoji in it should
  // export with a box where the emoji was, not fail to export.
  std::uint16_t glyph(char32_t codepoint) const;

  // The glyph's advance, in the 1000-unit em PDF measures text in. Every width
  // this class reports is in that unit, because it is the only one any caller
  // has a use for and converting in one place is what stops the two unit
  // systems being mixed up at a call site.
  int advance(std::uint16_t glyph) const;

  // How much the face wants `right` moved toward `left` when it follows it, in
  // the same 1000-unit em -- negative for the tightening almost all kerning
  // is, and zero for the overwhelming majority of pairs.
  //
  // Read here rather than left to a shaper because the *measurement* has to
  // include it: a run's width and the glyphs written into the content stream
  // come from this one object precisely so that the line breaks the layout
  // computed are the line breaks the file draws. See `PdfFont`.
  int kerning(std::uint16_t left, std::uint16_t right) const;

  bool kerns() const { return !kern_.empty(); }

  // The whole file, for embedding. A subset would be smaller -- see
  // `docs/tech-debt.md` -- but a subset is a font that has to be rebuilt, and
  // an unsubsetted embed is a font that is simply copied.
  const std::string& bytes() const { return bytes_; }
  // Whether the outlines are CFF (an OpenType/CFF file, `OTTO`) rather than
  // `glyf`. It decides which of the two PDF font shapes the file goes into,
  // and nothing else.
  bool isCff() const { return isCff_; }

  std::uint16_t glyphCount() const { return glyphCount_; }

  // `/FontDescriptor`, in 1000-unit em throughout.
  int ascent() const { return ascent_; }
  int descent() const { return descent_; }   // negative
  int capHeight() const { return capHeight_; }
  int italicAngle() const { return italicAngle_; }
  int stemV() const { return stemV_; }
  int bboxMinX() const { return bboxMinX_; }
  int bboxMinY() const { return bboxMinY_; }
  int bboxMaxX() const { return bboxMaxX_; }
  int bboxMaxY() const { return bboxMaxY_; }
  bool fixedPitch() const { return fixedPitch_; }

private:
  SfntFont() = default;
  bool parse();
  // A table's bytes, empty when the file has no such table.
  std::string_view table(const char* tag) const;
  bool parseCmap();
  std::uint16_t lookupFormat4(char32_t codepoint) const;
  std::uint16_t lookupFormat12(char32_t codepoint) const;
  // Font units to the 1000-unit em. One place, because a value converted at
  // some call sites and not others is the bug this rounds away.
  int toEm(int units) const;

  std::string bytes_;
  // Where the tables are, resolved once. Offsets into `bytes_` rather than
  // views, so the strings stay valid across the move `open` returns by.
  struct Table {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
  };
  Table head_, hhea_, hmtx_, maxp_, os2_, post_, cmap_, gpos_;
  KernPairs kern_;

  bool isCff_ = false;
  int unitsPerEm_ = 1000;
  std::uint16_t glyphCount_ = 0;
  std::uint16_t hMetricCount_ = 0;
  // The chosen cmap subtable, and which shape it is. Resolved at parse time so
  // a lookup is a branch rather than a search through the table directory.
  std::uint32_t cmapOffset_ = 0;
  int cmapFormat_ = 0;

  int ascent_ = 800;
  int descent_ = -200;
  int capHeight_ = 700;
  int italicAngle_ = 0;
  int stemV_ = 80;
  int bboxMinX_ = 0;
  int bboxMinY_ = -200;
  int bboxMaxX_ = 1000;
  int bboxMaxY_ = 900;
  bool fixedPitch_ = false;
};

}
