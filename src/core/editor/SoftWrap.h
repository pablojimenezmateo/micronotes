#pragma once

#include "core/editor/TextEdit.h"

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

// The whole buffer, wrapped. `softWrapInto` is the same thing over a vector the
// caller owns, so the last wrap's allocation is reused rather than freed and
// taken again -- a 200 KB note is 19,000 rows and 300 KB of them.
void softWrapInto(std::vector<SoftWrapRow>& rows, std::string_view text, int width,
                  const MeasureText& measure);
std::vector<SoftWrapRow> softWrap(std::string_view text, int width, const MeasureText& measure);

// Scratch an incremental wrap reuses, so it allocates nothing after the first
// call. Owned by the caller rather than hidden in a function-local static for
// the same reason `doc::FlowScratch` is: a static would be shared mutable state
// between surfaces that have no other relationship, and this tree keeps the
// perf tables as its only deliberate exception to that.
struct SoftWrapScratch {
  std::vector<SoftWrapRow> relaid;
};

// The same rows after one edit, without wrapping the lines the edit did not
// reach.
//
// `rows` must be the wrap of the buffer at `edit.fromRevision`, `text` the
// buffer at `edit.toRevision`, and the width and the face must be the ones
// `rows` was built with -- none of which this can check, so the caller checks
// them: that is what the revision stamps on `TextEdit` are for. Getting it
// wrong is a wrap that does not match the buffer, which is a caret in the wrong
// place rather than a crash, so the caller's check is the whole safety of it.
//
// Why it is sound. Rows partition the buffer, a logical line's rows are
// contiguous, and a line break inside `text` is a byte -- so an edit can only
// change the wrap of the logical lines its own bytes touch. Everything before
// the first of those is byte-identical and wraps identically; everything after
// is byte-identical too and merely sits `newEnd - oldEnd` further along, which
// is an addition per row rather than a measurement.
//
// Returns the number of rows it produced for the region it rewrapped, which is
// what says the increment is working: one keystroke should relay one line's
// worth of rows, not the note's.
std::size_t softWrapUpdate(std::vector<SoftWrapRow>& rows, SoftWrapScratch& scratch,
                           std::string_view text, const TextEdit& edit, int width,
                           const MeasureText& measure);

int rowForOffset(const std::vector<SoftWrapRow>& rows, std::size_t offset);
// The offset in `buffer` that `x` logical pixels into `row` addresses, snapped
// to a codepoint boundary and never past the row's end.
std::size_t offsetForRowX(std::string_view buffer, const SoftWrapRow& row, float x,
                          const MeasureText& measure);

}
