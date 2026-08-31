#include "ui/Fuzzy.h"

#include <cctype>

namespace micronotes::ui {
namespace {

char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool isBoundary(std::string_view text, std::size_t index) {
  if(index == 0) return true;
  const char previous = text[index - 1];
  return previous == ' ' || previous == '-' || previous == '_' || previous == '/' || previous == '.';
}

}

std::optional<int> fuzzyScore(std::string_view text, std::string_view query) {
  if(query.empty()) return 0;
  if(query.size() > text.size()) return std::nullopt;

  int score = 0;
  int run = 0;
  std::size_t at = 0;
  for(const char raw : query) {
    const char needle = lower(raw);
    bool found = false;
    while(at < text.size()) {
      const bool hit = lower(text[at]) == needle;
      if(hit) {
        score += 10;
        if(isBoundary(text, at)) score += 15;
        run = run > 0 ? run + 1 : 1;
        score += run * 5;
        ++at;
        found = true;
        break;
      }
      run = 0;
      ++at;
    }
    if(!found) return std::nullopt;
  }

  // Prefer the shorter of two otherwise equal candidates.
  score -= static_cast<int>(text.size()) / 4;
  return score;
}

}
