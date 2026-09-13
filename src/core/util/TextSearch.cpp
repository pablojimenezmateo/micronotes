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


// The next match at or after `from`, or `{text.size(), text.size()}` when there
// is none. The one place the candidate walk and the whole-word filter meet, so
// the cold scan and the incremental one cannot come to disagree about what a
// match is -- which is the failure this whole file was written out of.
TextMatch nextMatchFrom(std::string_view text, std::string_view needle, SearchOptions options,
                        std::size_t from) {
  for(std::size_t at = from; at + needle.size() <= text.size();) {
    at = nextCandidate(text, needle, at, options.matchCase);
    if(at + needle.size() > text.size()) break;
    const std::size_t end = at + needle.size();
    if(!options.wholeWord || standsAlone(text, at, end)) return {at, end};
    // A rejected candidate is not a match, so the bytes it covers are still
    // searchable: step one byte, not the whole needle. `for` in `nextCandidate`
    // resumes from here.
    ++at;
  }
  return {text.size(), text.size()};
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
  for(TextMatch match = nextMatchFrom(text, needle, options, 0); match.start < text.size();
      match = nextMatchFrom(text, needle, options, match.end)) {
    if(out->size() == kMaxMatches) {
      if(truncated) *truncated = true;
      perf::addCounter(perf::CounterId::TextSearchTruncated);
      return;
    }
    out->push_back(match);
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

namespace {

// First index whose end is > offset, or matches.size(). Ends ascend because the
// matches do and do not overlap, so this is a binary search like the two above.
std::size_t upperBoundByEnd(const std::vector<TextMatch>& matches, std::size_t offset) {
  const auto at = std::upper_bound(matches.begin(), matches.end(), offset,
                                   [](std::size_t position, const TextMatch& match) {
                                     return position < match.end;
                                   });
  return static_cast<std::size_t>(at - matches.begin());
}

bool declineUpdate() {
  perf::addCounter(perf::CounterId::TextSearchUpdateDeclines);
  return false;
}

}

// The three regions, and why each boundary is where it is.
//
// **What is kept.** A match is untouched by the edit when every byte it is made
// of *and* the byte on either side of it -- the whole-word predicate reads those
// -- lies before `edit.start`. Taking the reach back as the needle's whole length
// rather than `length - 1` covers the far side of that predicate in the same
// subtraction: a candidate ending exactly at `edit.start` is judged on a byte the
// edit rewrote, so it has to be re-judged.
//
// **Where the rescan begins.** Not at the kept prefix's end, which can be a long
// way back: the *old* scan resumed there and found nothing before the first match
// it kept out, so the new scan finds nothing there either -- except a match that
// reaches forward into the edited bytes, and one of those cannot begin before
// `windowStart`. So the scan starts at whichever of the two comes first.
//
// **Where it ends.** This is the part that is not a transcription of
// `softWrapUpdate`, which knows the line it finished on. Here the rescan can emit
// a match running past the point the old list resumes at, so there is no computed
// splice index -- the scan is walked forward a match at a time until its cursor
// lands where the old scan also had a boundary: past the edit, at or before some
// old match's start, and at or after the previous one's end. From there the two
// buffers are byte-identical and the greedy walk over them is the same walk, so
// the rest of the old list is the rest of the answer with `delta` added.
bool findAllUpdate(std::vector<TextMatch>* matches, std::vector<TextMatch>* scratch,
                   bool* truncated, std::string_view text, std::string_view needle,
                   SearchOptions options, const editor::TextEdit& edit) {
  if(!edit.known()) return declineUpdate();
  if(needle.empty() || needle.size() > text.size()) return declineUpdate();
  // A capped list cannot be spliced: the cap's position moves with an insertion,
  // and what fell off the end was never recorded.
  if(truncated == nullptr || *truncated) return declineUpdate();
  if(edit.start > edit.oldEnd || edit.start > edit.newEnd || edit.newEnd > text.size()) {
    return declineUpdate();
  }

  std::vector<TextMatch>& out = *matches;
  const std::ptrdiff_t delta =
    static_cast<std::ptrdiff_t>(edit.newEnd) - static_cast<std::ptrdiff_t>(edit.oldEnd);
  const std::size_t windowStart = edit.start > needle.size() ? edit.start - needle.size() : 0;
  const std::size_t keep = upperBoundByEnd(out, windowStart);
  const std::size_t scanFrom =
    keep < out.size() ? std::min(out[keep].start, windowStart) : windowStart;

  scratch->clear();
  std::size_t at = scanFrom;
  std::size_t tail = out.size();
  while(true) {
    // Past the edit, the two buffers are the same bytes: try to land on a
    // boundary the old walk also had.
    if(at >= edit.newEnd) {
      const std::size_t atOld = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at) - delta);
      const std::size_t next = lowerBoundByStart(out, atOld);
      const bool behindIsClear = next == 0 || out[next - 1].end <= atOld;
      // `> oldEnd` and not `>= oldEnd`: the byte before a kept match is what the
      // whole-word predicate reads, and the byte at `oldEnd` is the first the
      // edit replaced.
      const bool aheadIsClear = next == out.size() || out[next].start > edit.oldEnd;
      if(behindIsClear && aheadIsClear) {
        tail = next;
        break;
      }
    }
    const TextMatch match = nextMatchFrom(text, needle, options, at);
    if(match.start >= text.size()) {
      // The rescan ran to the end of the buffer, so there is no tail to keep.
      tail = out.size();
      break;
    }
    if(keep + scratch->size() >= kMaxMatches) return declineUpdate();
    scratch->push_back(match);
    at = match.end;
  }

  const std::size_t middle = scratch->size();
  const std::size_t tailCount = out.size() - tail;
  const std::size_t total = keep + middle + tailCount;
  if(total > kMaxMatches) return declineUpdate();

  perf::addCounter(perf::CounterId::TextSearchUpdates);
  perf::addCounter(perf::CounterId::TextSearchUpdateBytes, at > scanFrom ? at - scanFrom : 0);

  if(keep + middle < tail) {
    std::move(out.begin() + static_cast<std::ptrdiff_t>(tail), out.end(),
              out.begin() + static_cast<std::ptrdiff_t>(keep + middle));
    out.resize(total);
  } else if(keep + middle > tail) {
    out.resize(total);
    std::move_backward(out.begin() + static_cast<std::ptrdiff_t>(tail),
                       out.begin() + static_cast<std::ptrdiff_t>(tail + tailCount),
                       out.begin() + static_cast<std::ptrdiff_t>(total));
  }
  std::copy(scratch->begin(), scratch->end(), out.begin() + static_cast<std::ptrdiff_t>(keep));
  for(std::size_t i = keep + middle; i < total; ++i) {
    out[i].start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(out[i].start) + delta);
    out[i].end = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(out[i].end) + delta);
  }
  return true;
}

}
