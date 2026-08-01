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

// Byte offset of the code point boundary at or before `offset`.
std::size_t previousBoundary(std::string_view text, std::size_t offset);

// Byte offset of the next code point boundary after `offset`.
std::size_t nextBoundary(std::string_view text, std::size_t offset);

// Number of code points in `text`. Used for column arithmetic, where counting
// bytes would put the caret in the wrong place on any line containing
// non-ASCII.
std::size_t countCodePoints(std::string_view text);

// Byte offset of the `index`-th code point, clamped to the end of `text`.
std::size_t offsetForCodePoint(std::string_view text, std::size_t index);

}
