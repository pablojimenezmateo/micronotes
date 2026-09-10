#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace microcore::editor {

// One visual row of a soft-wrapped buffer, as a span of it.
//
// It used to carry a `std::string` copy of its own text as well, which made the
// wrap of a note cost a copy of the note: 3,197 allocations and 1.5 MB for a
// 200 KB one, rebuilt on every keystroke because the raw pane's memo is keyed on
// the editor's revision. The bytes were already in the buffer the wrap was
// handed, and every reader of them had that buffer in scope.
//
// So the buffer is a parameter now, at every site that wants a row's text --
// `textIn` below, and `offsetForRowX`. That is deliberately not the same thing
// as putting a `string_view` in the row: a view would make the row *look*
// self-contained while quietly depending on a buffer whose next edit
// reallocates it, and the failure would be a stale row drawn from freed memory.
// Naming the buffer at the call site is the lifetime, stated.
struct SoftWrapRow {
  std::size_t start = 0;
  std::size_t end = 0;
};

using MeasureText = std::function<int(std::string_view)>;

// The row's own bytes, out of the buffer it was wrapped from. Handing this a
// different buffer than `softWrap` was given is a bug the type cannot catch;
// handing it the same one is free.
inline std::string_view textIn(std::string_view buffer, const SoftWrapRow& row) {
  if(row.start >= buffer.size()) return {};
  return buffer.substr(row.start, std::min(row.end, buffer.size()) - row.start);
}

std::vector<SoftWrapRow> softWrap(std::string_view text, int width, const MeasureText& measure);
int rowForOffset(const std::vector<SoftWrapRow>& rows, std::size_t offset);
// The offset in `buffer` that `x` logical pixels into `row` addresses, snapped
// to a codepoint boundary and never past the row's end.
std::size_t offsetForRowX(std::string_view buffer, const SoftWrapRow& row, float x,
                          const MeasureText& measure);

}
