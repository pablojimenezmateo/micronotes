#pragma once

#include "core/editor/SingleLineView.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace microcore::editor {

// One display line of a wrapped buffer, addressed by byte offset into the
// source it came from.
struct WrappedLine {
  std::size_t begin = 0;  // first byte drawn
  std::size_t end = 0;    // one past the last byte drawn
  std::size_t next = 0;   // where the following line begins; always >= end
};

// Wraps text for display *without rewriting it*.
//
// The obvious way to wrap -- split on whitespace, re-join with single spaces --
// is right for rendered markdown and wrong for an editor twice over. It shows
// text the user did not type: two spaces come back as one, and a tab becomes a
// space. And because the drawn string no longer matches any range of the
// source, there is no way to say which byte a screen position corresponds to.
// A caret, a selection band and a click-to-place hit test all need exactly that
// mapping, so every line here keeps its source range.
//
// `next` exists so that every caret offset falls inside exactly one line,
// including one parked in the run of whitespace a wrap consumed -- which is
// otherwise a gap between one line's `end` and the next line's `begin` where a
// caret would simply not be drawn.
std::vector<WrappedLine> wrapLines(std::string_view source, float width, const TextWidthFn& measure);

}
