#include "core/markdown/BareUrl.h"

#include <cctype>

namespace microcore::markdown {
namespace {

bool startsWithScheme(std::string_view text, std::size_t pos) {
  return text.substr(pos, 7) == "http://" || text.substr(pos, 8) == "https://";
}

bool isTerminator(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '<' || c == '>';
}

// Only what a sentence puts *after* a URL. A query string is full of `=` and
// `&` and `%`, and a SharePoint link ends in a base64 blob, so trimming
// anything wider than this would cut the URL itself short.
bool isTrailingPunctuation(char c) {
  return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '"' || c == '\'';
}

}

BareUrl bareUrlAt(std::string_view text, std::size_t pos) {
  if(pos >= text.size() || !startsWithScheme(text, pos)) return {};
  std::size_t end = pos;
  while(end < text.size() && !isTerminator(text[end])) ++end;
  std::size_t trimmed = end;
  while(trimmed > pos && isTrailingPunctuation(text[trimmed - 1])) --trimmed;
  // A run that trims away to nothing was punctuation wearing a URL's clothes.
  if(trimmed == pos) return {0, end - pos};
  return {trimmed - pos, end - pos};
}

}
