#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::doc {

enum class SpanKind {
  Emphasis,
  Strong,
  Code,
  Strike,
  Link,
  Image,
  Autolink,
  Escape
};

// Marker ranges are recorded separately from the content range: that separation
// is what lets the live surface hide `**` without touching a byte of the file.
struct SourceSpan {
  SpanKind kind = SpanKind::Emphasis;
  std::size_t start = 0;
  std::size_t end = 0;
  std::size_t contentStart = 0;
  std::size_t contentEnd = 0;
  // [openStart, openEnd) and [closeStart, closeEnd) are the syntax characters.
  // For links the closing marker spans "](target)" in full.
  std::size_t openStart = 0;
  std::size_t openEnd = 0;
  std::size_t closeStart = 0;
  std::size_t closeEnd = 0;
  std::string target;  // link/image destination, or the autolink URL
  int depth = 0;
};

// `text` is the block's content; every returned offset is `base` plus an offset
// into `text`, so spans address the note buffer directly.
std::vector<SourceSpan> scanInlines(std::string_view text, std::size_t base = 0);

}
