#include "core/util/TextSearch.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/StringUtil.h"

#include <algorithm>

namespace microcore::util {
namespace {

// Case-insensitive byte compare of `needle` against `text` at `at`.
//
// Folded per byte rather than by lowering a copy of the note: the fold is
// length-preserving over ASCII, so an offset into the folded text is an offset
// into the original, and a scan that lowers the haystack pays a copy of the
// whole note per keystroke to save nothing.
bool equalAt(std::string_view text, std::size_t at, std::string_view needle, bool matchCase) {
  if(at + needle.size() > text.size()) return false;
  if(matchCase) return text.compare(at, needle.size(), needle) == 0;
  for(std::size_t i = 0; i < needle.size(); ++i) {
    if(toLowerAscii(text[at + i]) != toLowerAscii(needle[i])) return false;
  }
  return true;
}

// The next candidate start at or after `from`, ignoring the whole-word filter.
//
// The case-sensitive arm hands the work to `std::string_view::find`, which is
// the library's tuned search; the folded arm walks the first byte itself,
// because there is no folded `find` and lowering the haystack to get one is the
// copy this whole file is written to avoid.
std::size_t nextCandidate(std::string_view text, std::string_view needle, std::size_t from,
                          bool matchCase) {
  if(matchCase) {
    const std::size_t at = text.find(needle, from);
    return at == std::string_view::npos ? text.size() : at;
  }
  const char first = toLowerAscii(needle.front());
  for(std::size_t at = from; at + needle.size() <= text.size(); ++at) {
    if(toLowerAscii(text[at]) != first) continue;
    if(equalAt(text, at, needle, false)) return at;
  }
  return text.size();
}

}

bool standsAlone(std::string_view text, std::size_t start, std::size_t end) {
  if(start > 0 && start <= text.size() && isWordByte(text[start - 1])) return false;
  return end >= text.size() || !isWordByte(text[end]);
}

std::size_t findFrom(std::string_view text, std::string_view needle, std::size_t from,
                     SearchOptions options) {
  if(needle.empty() || needle.size() > text.size()) return text.size();
  for(std::size_t at = std::min(from, text.size()); at + needle.size() <= text.size(); ++at) {
    at = nextCandidate(text, needle, at, options.matchCase);
    if(at + needle.size() > text.size()) break;
    if(!options.wholeWord || standsAlone(text, at, at + needle.size())) return at;
  }
  return text.size();
}

void findAllInto(std::string_view text, std::string_view needle, SearchOptions options,
                 std::vector<TextMatch>* out, bool* truncated) {
  out->clear();
  if(truncated) *truncated = false;
  if(needle.empty() || needle.size() > text.size()) return;
  perf::addCounter(perf::CounterId::TextSearchScans);
  perf::addCounter(perf::CounterId::TextSearchScanBytes, text.size());
  // Non-overlapping: a match consumes its own bytes, so the next scan starts
  // where this one ended rather than one byte in.
  for(std::size_t at = 0; at + needle.size() <= text.size();) {
    at = nextCandidate(text, needle, at, options.matchCase);
    if(at + needle.size() > text.size()) break;
    const std::size_t end = at + needle.size();
    if(!options.wholeWord || standsAlone(text, at, end)) {
      if(out->size() == kMaxMatches) {
        if(truncated) *truncated = true;
        perf::addCounter(perf::CounterId::TextSearchTruncated);
        return;
      }
      out->push_back({at, end});
      at = end;
      continue;
    }
    // A rejected candidate is not a match, so the bytes it covers are still
    // searchable: step one byte, not the whole needle. `for` in `nextCandidate`
    // resumes from here.
    ++at;
  }
}

std::vector<TextMatch> findAll(std::string_view text, std::string_view needle,
                               SearchOptions options, bool* truncated) {
  std::vector<TextMatch> matches;
  findAllInto(text, needle, options, &matches, truncated);
  return matches;
}

namespace {

// First index whose start is >= offset, or matches.size() when there is none.
std::size_t lowerBoundByStart(const std::vector<TextMatch>& matches, std::size_t offset) {
  const auto at = std::lower_bound(matches.begin(), matches.end(), offset,
                                   [](const TextMatch& match, std::size_t position) {
                                     return match.start < position;
                                   });
  return static_cast<std::size_t>(at - matches.begin());
}

// First index whose start is > offset, or matches.size() when there is none.
std::size_t upperBoundByStart(const std::vector<TextMatch>& matches, std::size_t offset) {
  const auto at = std::upper_bound(matches.begin(), matches.end(), offset,
                                   [](std::size_t position, const TextMatch& match) {
                                     return position < match.start;
                                   });
  return static_cast<std::size_t>(at - matches.begin());
}

}

std::size_t matchAtOrAfter(const std::vector<TextMatch>& matches, std::size_t offset) {
  const std::size_t at = lowerBoundByStart(matches, offset);
  return at == matches.size() ? 0 : at;
}

std::size_t matchAfter(const std::vector<TextMatch>& matches, std::size_t offset) {
  if(matches.empty()) return 0;
  const std::size_t at = upperBoundByStart(matches, offset);
  return at == matches.size() ? 0 : at;
}

std::size_t matchBefore(const std::vector<TextMatch>& matches, std::size_t offset) {
  if(matches.empty()) return 0;
  const std::size_t at = lowerBoundByStart(matches, offset);
  return at == 0 ? matches.size() - 1 : at - 1;
}

}
