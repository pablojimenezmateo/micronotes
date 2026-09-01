#pragma once

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

// A link target that leaves the machine, as opposed to one inside the library.
bool isRemoteTarget(std::string_view target);

// What to call a clipboard image, given the MIME type it arrived as.
std::string fileNameForMime(std::string_view mime);

// Tags round-trip through a space-separated line in the tag editor.
std::vector<std::string> splitTags(std::string_view value);
std::string joinTags(const std::vector<std::string>& tags);

}
