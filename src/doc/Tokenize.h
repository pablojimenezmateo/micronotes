#pragma once

#include "doc/Layout.h"

#include "core/util/StringUtil.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Turning a block's source into tokens: the other half of laying one out, and
// the half `doc/Flow.h` consumes.
//
// A token is a maximal run of source bytes sharing one style, split at every
// space and at every change of inline attribute. The flow then decides where
// the lines fall, and between them that is the whole of a block's text layout.
//
// Header-inline for the same measured reason `Flow` is: this runs once per
// token of every relaid block, Release carries no LTO, and these were in
// `Layout.cpp`'s anonymous namespace -- inlined into the staging that calls
// them -- until they were moved out of it.
namespace micronotes::doc {

// The one whitespace predicate, from `core/util/StringUtil.h`. This file and
// the editor's word walk each carried a copy naming four bytes; the shared one
// names six, and a form feed is whitespace in both of the places they are used.
using microcore::util::isAsciiSpace;

// Newlines and tabs become one space each, so a run's text stays byte-aligned
// with the source it came from and prefix measurement maps offsets to pixels.
inline std::string displayText(std::string_view source) {
  std::string out(source);
  for(char& c : out) {
    if(c == '\n' || c == '\t' || c == '\r') c = ' ';
  }
  return out;
}

inline Token makeToken(std::string_view source, std::size_t start, std::size_t end, const RunStyle& style,
                TextRole role, bool marker, bool hidden, int link, bool lineBreak = false) {
  Token token;
  token.start = start;
  token.end = end;
  token.style = style;
  token.role = role;
  token.isMarker = marker;
  token.hidden = hidden;
  token.link = link;
  token.lineBreak = lineBreak;
  if(!hidden) token.text = displayText(source.substr(start, end - start));
  token.space = !token.text.empty() && std::all_of(token.text.begin(), token.text.end(), [](char c) { return c == ' '; });
  return token;
}

inline RunStyle styleFrom(const RunStyle& base, const Attr& attr, float monoSize) {
  RunStyle style = base;
  if(attr.strong) style.strong = true;
  if(attr.italic) style.italic = true;
  if(attr.strike) style.strike = true;
  if(attr.mono) {
    style.mono = true;
    style.size = monoSize;
  }
  return style;
}

// Tokens a span of content is about to produce, near enough. Words and the
// spaces between them alternate, and English prose runs about five bytes to the
// word, so a span yields roughly one token per three bytes. It only has to be
// close: the point is that the token vector grows once instead of the five or
// six doublings it took to reach a paragraph's worth from empty, which across a
// document was the single largest source of allocation in laying one out.
inline std::size_t tokenEstimate(std::size_t bytes) {
  return bytes / 3 + 4;
}

// The same split for a block the inline scanner found nothing in, which in
// ordinary prose is most of them. Worth its own loop because the general one
// pays for markup this block does not have: a heap-allocated attribute slot per
// content byte, zero-filled and then read back, to conclude that every byte is
// plain. Kept directly below its general form so the two stay in step.
inline void appendPlainTokens(std::string_view source, std::size_t from, std::size_t to,
                       const RunStyle& base, LineGroup& out) {
  out.reserve(out.size() + tokenEstimate(to - from));
  std::size_t i = from;
  while(i < to) {
    const bool space = isAsciiSpace(source[i]);
    std::size_t j = i + 1;
    while(j < to && isAsciiSpace(source[j]) == space) ++j;
    const bool endsLine = space && source.substr(i, j - i).find('\n') != std::string_view::npos;
    if(endsLine && j - i > 1) {
      out.push_back(makeToken(source, i, j - 1, base, TextRole::Body, false, true, -1));
      out.push_back(makeToken(source, j - 1, j, base, TextRole::Body, false, false, -1, true));
      i = j;
      continue;
    }
    out.push_back(makeToken(source, i, j, base, TextRole::Body, false, false, -1, endsLine));
    i = j;
  }
}

// Splits `[from, to)` into tokens that share one set of inline attributes, with
// whitespace kept as its own token so wrapping has break opportunities.
inline void appendContentTokens(std::string_view source, std::size_t from, std::size_t to, const std::vector<Attr>& attrs,
                         const RunStyle& base, float monoSize, bool revealed, LineGroup& out) {
  out.reserve(out.size() + tokenEstimate(to - from));
  std::size_t i = from;
  while(i < to) {
    const Attr& attr = attrs[i - from];
    const bool space = isAsciiSpace(source[i]);
    std::size_t j = i + 1;
    while(j < to && attrs[j - from] == attr && isAsciiSpace(source[j]) == space) ++j;
    const bool hidden = attr.marker && !revealed;
    const RunStyle style = styleFrom(base, attr, monoSize);
    const TextRole role = attr.marker ? TextRole::Marker : attr.role;
    // A line ending inside a block takes however many bytes it took -- the
    // newline itself, plus the indentation of the line continuing it -- and
    // ends the line on screen. All but the last byte are emitted hidden, which
    // is zero width and still addressable, so the source stays byte-aligned
    // with what is drawn.
    const bool endsLine =
      space && !hidden && source.substr(i, j - i).find('\n') != std::string_view::npos;
    if(endsLine && j - i > 1) {
      out.push_back(makeToken(source, i, j - 1, style, role, attr.marker, true, -1));
      out.push_back(makeToken(source, j - 1, j, style, role, attr.marker, false,
                              attr.marker ? -1 : attr.link, true));
      i = j;
      continue;
    }
    out.push_back(makeToken(source, i, j, style, role, attr.marker, hidden,
                            attr.marker ? -1 : attr.link, endsLine));
    i = j;
  }
}

// Into a buffer the caller owns, for the same reason everything else on this
// path is: a fenced code block or a block dropped to raw asked for one of these

inline void sourceLinesInto(std::string_view source, std::size_t from, std::size_t to,
                     std::vector<std::pair<std::size_t, std::size_t>>* out) {
  std::vector<std::pair<std::size_t, std::size_t>>& lines = *out;
  lines.clear();
  std::size_t start = from;
  while(start < to) {
    auto newline = source.find('\n', start);
    if(newline == std::string_view::npos || newline >= to) newline = to;
    lines.push_back({start, newline});
    start = newline < to ? newline + 1 : to;
    if(newline == to) break;
  }
  if(lines.empty()) lines.push_back({from, to});
}

}
