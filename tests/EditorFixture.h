#pragma once

#include <cstddef>
#include <string_view>

namespace micronotes::tests {

// The word count from scratch, by the definition the status bar means: a word
// is a run of non-space bytes.
//
// Deliberately spelled differently from the editor's incremental version -- a
// reference implementation that shares the production one's structure checks
// nothing. Shared between `EditorTests.cpp` and `EditorUndoTests.cpp`, because
// the count has to survive undo as well as editing, and those are the two files
// that ask.
inline std::size_t wordsFromScratch(std::string_view text) {
  std::size_t words = 0;
  bool inWord = false;
  for(const unsigned char c : text) {
    const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if(space) {
      inWord = false;
      continue;
    }
    if(!inWord) ++words;
    inWord = true;
  }
  return words;
}

}
