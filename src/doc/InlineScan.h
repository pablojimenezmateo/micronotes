#pragma once

#include <cstddef>
#include <cstdint>
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
  // [[Another note]], or [[Another note|what to call it here]]. Not CommonMark,
  // but the spelling every notes app that has links between notes settled on,
  // and it survives being read as plain text, which is what the library format
  // asks of anything written into a .md.
  WikiLink,
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
  std::string target;  // link/image destination, the autolink URL, or the wikilink target
  int depth = 0;
};

// The buffers the scan needs and can hand back: the spans it produces and a
// byte mask over the text saying which bytes structural scanning has claimed.
// Both are one allocation, and the scan runs once per block -- so a caller that
// scans a whole document holds one of these instead of paying for them ten
// thousand times.
//
// On a cold open of a 200 KB note the two of them were close to a quarter of
// everything the layout allocated, and the mask was paid even by the four
// blocks in five that have no markup in them at all: allocated, zero-filled,
// and then read by a scan that found nothing to write in it.
struct InlineScratch {
  std::vector<SourceSpan> spans;
  std::vector<char> masked;
  // An unmatched `*`, `_` or `~` run the emphasis pass is still holding open.
  struct Delimiter {
    std::size_t pos = 0;
    std::size_t length = 0;
    char marker = '*';
  };
  // The emphasis pass's open-delimiter stack and the nesting-depth pass's end
  // stack. Both were function locals, so every block carrying markup allocated
  // two vectors and dropped them again a few microseconds later. Held here they
  // are grown once for a document.
  std::vector<Delimiter> delimiters;
  std::vector<std::size_t> ends;
};

// `text` is the block's content; every returned offset is `base` plus an offset
// into `text`, so spans address the note buffer directly.
//
// The result lives in `scratch->spans` and stands until the next scan through
// the same scratch.
const std::vector<SourceSpan>& scanInlinesInto(std::string_view text, std::size_t base,
                                               InlineScratch* scratch);

// The same scan, allocating and dropping its own buffers. Right for a one-off;
// on a path that runs per block, hold an `InlineScratch` and use the form above.
std::vector<SourceSpan> scanInlines(std::string_view text, std::size_t base = 0);

}
