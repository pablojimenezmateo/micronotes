#pragma once

#include <cstddef>
#include <string_view>

namespace microcore::util {

// UTF-8 boundary arithmetic over byte offsets.
//
// The editor stores text as UTF-8 bytes and addresses the caret by byte offset,
// which is the right representation -- it makes every edit an ordinary string
// splice. What it must not do is *move* the caret a byte at a time: stepping
// left through "café" one byte at a time lands in the middle of the combining
// accent, and erasing one byte from there leaves an invalid sequence that
// renders as a replacement character. These helpers step whole code points
// instead.

// True for a UTF-8 continuation byte (10xxxxxx) -- a byte that is part of a
// multi-byte sequence but not its start.
constexpr bool isContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

// Byte offset of the code point boundary strictly before `offset` -- one step
// left. This is what Backspace and a left arrow move by.
std::size_t previousBoundary(std::string_view text, std::size_t offset);

// Byte offset of the code point boundary at or before `offset` -- a snap, not a
// step, so an offset already on a boundary is returned unchanged. This is what
// a truncation or a hit test does with a byte index it computed by arithmetic:
// the index is where the cut should go, and the only question is whether it is
// in the middle of a sequence.
std::size_t boundaryAtOrBefore(std::string_view text, std::size_t offset);

// Byte offset of the next code point boundary after `offset`.
std::size_t nextBoundary(std::string_view text, std::size_t offset);

// Number of code points in `text`. Used for column arithmetic, where counting
// bytes would put the caret in the wrong place on any line containing
// non-ASCII.
std::size_t countCodePoints(std::string_view text);

}
