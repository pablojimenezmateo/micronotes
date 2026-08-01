#include "core/editor/WrappedLines.h"

#include "core/perf/PerformanceCounters.h"

#include <algorithm>

namespace microcore::editor {
namespace {

bool isBreakByte(char c) {
  return c == ' ' || c == '\t';
}

}

std::vector<WrappedLine> wrapLines(std::string_view source, float width, const TextWidthFn& measure) {
  std::vector<WrappedLine> lines;
  std::size_t paragraphStart = 0;
  while(true) {
    const auto newline = source.find('\n', paragraphStart);
    const std::size_t paragraphEnd = newline == std::string_view::npos ? source.size() : newline;

    // Greedy: extend the line one word at a time, and break before the first
    // word that pushes it past `width`. One measurement per word, same cost as
    // the wrapper this replaces.
    std::size_t lineStart = paragraphStart;
    std::size_t lastFit = paragraphStart;
    std::size_t scan = paragraphStart;
    while(scan < paragraphEnd) {
      std::size_t wordEnd = scan;
      while(wordEnd < paragraphEnd && !isBreakByte(source[wordEnd])) ++wordEnd;
      std::size_t following = wordEnd;
      while(following < paragraphEnd && isBreakByte(source[following])) ++following;

      perf::addCounter(perf::CounterId::EditorWrapMeasures);
      const bool overflows =
        static_cast<float>(measure(source.substr(lineStart, wordEnd - lineStart))) > width;
      // A single word wider than the line stays on its own line rather than
      // vanishing: breaking before it when nothing precedes it would loop.
      if(overflows && lastFit > lineStart) {
        lines.push_back({lineStart, lastFit, scan});
        lineStart = scan;
      }
      lastFit = wordEnd;
      scan = following;
    }
    lines.push_back({lineStart, paragraphEnd, paragraphEnd});

    if(newline == std::string_view::npos) break;
    paragraphStart = newline + 1;
  }

  perf::addCounter(perf::CounterId::EditorWrapLines, lines.size());
  for(std::size_t i = 0; i + 1 < lines.size(); ++i) lines[i].next = lines[i + 1].begin;
  // One past the end, so a caret sitting at the very end of the buffer still
  // falls inside the last line.
  if(!lines.empty()) lines.back().next = source.size() + 1;
  return lines;
}

}
