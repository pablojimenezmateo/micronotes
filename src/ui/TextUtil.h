#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// Small pure string helpers the shell needs. They lived in Application.cpp's
// anonymous namespace, where nothing could reach them and nothing could test
// them, which is how "the first non-blank line, with its heading marks stripped"
// ends up meaning something slightly different in two places.

// A note's title, taken from the first line that has anything on it.
std::string trimTitle(std::string_view text);

std::vector<std::string> splitLines(std::string_view text);

// Truncates to `limit` characters, spending three of them on the ellipsis.
std::string ellipsize(std::string text, std::size_t limit);

// The longest prefix of `value` that fits in `maxWidth` once an ellipsis is
// appended, or `value` itself when the whole thing fits.
//
// Truncation happens at code point boundaries, never mid-character: chopping
// bytes off a UTF-8 string hands the renderer something that is not text.
//
// The prefix is found by bisection, so a label shortened by a word costs a
// handful of measurements rather than one per character removed -- and
// `measure` is a shaping pass over the whole candidate, which is the expensive
// thing here.
//
// Split from the renderer so it can be tested without a font: the geometry is
// deterministic given a measurer, and the measurer is the only part that needs
// SDL.
std::string ellipsizeToFit(std::string value, int maxWidth,
                           const std::function<int(std::string_view)>& measure);

// The length in bytes of the longest prefix of `value` that measures within
// `maxWidth`, cut at a code point boundary. The other half of
// `ellipsizeToFit`: same bisection, for the case where the rest of the text
// goes on the next line instead of being replaced by an ellipsis.
//
// Never returns zero for a non-empty input -- one code point that does not fit
// still has to be put somewhere -- and never returns a length that lands inside
// a UTF-8 sequence, which is the bug this exists to make unwriteable.
//
// Like `ellipsizeToFit` it assumes measurement grows with the prefix, which is
// true of every font here and is what licenses the bisection.
std::size_t breakToFit(std::string_view value, int maxWidth,
                       const std::function<int(std::string_view)>& measure);

// A link target that leaves the machine, as opposed to one inside the library.
bool isRemoteTarget(std::string_view target);

// What to call a clipboard image, given the MIME type it arrived as.
std::string fileNameForMime(std::string_view mime);

// Tags round-trip through a space-separated line in the tag editor.
std::vector<std::string> splitTags(std::string_view value);
std::string joinTags(const std::vector<std::string>& tags);

}
