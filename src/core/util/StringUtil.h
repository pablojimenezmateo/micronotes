#pragma once

#include <string>
#include <string_view>

// The three string primitives the tree kept writing out by hand.
//
// Each one was duplicated, and in two of the three cases the copies had
// *drifted* -- which is the whole reason this file exists rather than being a
// convenience.
//
//   `trim` had three definitions. One took space and tab off both ends; one
//   took space, tab and CR off both; one took space and tab off the front and
//   space, tab and CR off the back. Nothing chose any of that: each was written
//   where it was needed, and the front matter parser's asymmetry in particular
//   is the sort of thing that reads as deliberate and is not.
//
//   The `tolower` dance -- `static_cast<char>(std::tolower(static_cast<unsigned
//   char>(c)))` -- appeared at eight sites. The inner cast is not decoration:
//   `std::tolower` takes an `int` that must be representable as `unsigned char`,
//   so passing a `char` straight in is undefined for any byte above 0x7F, which
//   in a notes app means any accented letter anybody types. Seven of the eight
//   had it; one did not.
//
// ASCII-only, deliberately and not as a shortcut. Case folding beyond ASCII is
// locale- and language-dependent -- Turkish dotless i, German sharp s -- and
// every caller here is folding a *machine* string: a file extension, a theme
// name, a key chord, a heading slug. A fold that changed with the user's locale
// would make those non-deterministic.
namespace microcore::util {

inline constexpr bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

inline constexpr bool isAsciiUpper(char c) {
  return c >= 'A' && c <= 'Z';
}

inline constexpr char toLowerAscii(char c) {
  return isAsciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

// Whitespace off both ends, as a view: the callers that want to own the result
// say `std::string(trim(x))`, and the ones that do not -- the front matter
// parser walks a line at a time -- stop allocating a string per line.
[[nodiscard]] constexpr std::string_view trim(std::string_view value) {
  while(!value.empty() && isAsciiSpace(value.front())) value.remove_prefix(1);
  while(!value.empty() && isAsciiSpace(value.back())) value.remove_suffix(1);
  return value;
}

[[nodiscard]] std::string toLowerAscii(std::string_view value);
void toLowerAsciiInPlace(std::string& value);

// Equal but for ASCII case. One pass, and no allocation: the two sites that
// wanted this each lowered a copy of both sides first.
[[nodiscard]] bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b);

}
