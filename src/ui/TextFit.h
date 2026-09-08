#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Making text fit a column, in the font it will actually be drawn in.
//
// This was the measured half of `ui/TextUtil.h`, which had become the tree's
// utility drawer: a trim, a lowercase, a line splitter, a tag parser, a MIME
// table, a `~`-path formatter and a URL decoder, in the *drawing* layer,
// reached from the library layer for the tag parser. Each of those has a real
// home and is now in it.
//
// What stayed is what genuinely belongs to a renderer: every function here
// takes a `measure`, because how much text fits is a question about a font and
// cannot be answered by counting bytes.
namespace micronotes::ui {

// The width of a run of text, in the font it is being laid out in.
using MeasureText = std::function<int(std::string_view)>;

// `value` shortened with a trailing ellipsis until it measures at most
// `maxWidth`. Cuts on UTF-8 boundaries, so a truncation never hands the
// renderer half a code point.
std::string ellipsizeToFit(std::string value, int maxWidth, const MeasureText& measure);

// How many bytes of `value` fit in `maxWidth`, on a UTF-8 boundary. For a
// caller that wraps rather than truncates.
std::size_t breakToFit(std::string_view value, int maxWidth, const MeasureText& measure);

// A window of `line` that contains the match and fits the column.
//
// The match itself is never ellipsized away -- which is the whole point, and
// what a plain `ellipsizeToFit` of the line cannot promise: a hit at the end of
// a long line would be exactly the part that got cut. `start` and `length` are
// where the match sits *inside the returned text*, so the draw can mark it
// without searching again.
struct SnippetWindow {
  std::string text;
  std::size_t start = 0;
  std::size_t length = 0;
};

SnippetWindow snippetAroundMatch(std::string_view line, std::size_t matchStart,
                                 std::size_t matchLength, int maxWidth, const MeasureText& measure);

}
