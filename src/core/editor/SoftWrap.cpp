#include "core/editor/SoftWrap.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <string_view>

namespace microcore::editor {
namespace {

bool isWrapSpace(char c) {
  return c != '\n' && std::isspace(static_cast<unsigned char>(c));
}

// Ensure `index` sits on a UTF-8 codepoint boundary inside (start, end], never
// splitting a multibyte character. Guarantees forward progress past `start`.
std::size_t snapToCodepoint(std::string_view text, std::size_t index, std::size_t start, std::size_t end) {
  if(index >= end) return end;
  const std::size_t snapped = util::boundaryAtOrBefore(text, index);
  if(snapped > start) return snapped;
  // A single codepoint wider than the row: advance past the whole codepoint.
  return std::min(util::nextBoundary(text, start), end);
}

int measuredWidth(std::string_view value, const MeasureText& measure) {
  return measure ? measure(value) : static_cast<int>(value.size());
}

void pushRow(std::vector<SoftWrapRow>& rows, std::size_t start, std::size_t end) {
  rows.push_back({start, end});
}

void wrapLogicalLine(std::vector<SoftWrapRow>& rows, std::string_view text, std::size_t start, std::size_t end, int width, const MeasureText& measure) {
  if(start == end) {
    pushRow(rows, start, end);
    return;
  }
  if(width <= 0 || measuredWidth(text.substr(start, end - start), measure) <= width) {
    pushRow(rows, start, end);
    return;
  }

  std::size_t pos = start;
  while(pos < end) {
    if(measuredWidth(text.substr(pos, end - pos), measure) <= width) {
      pushRow(rows, pos, end);
      break;
    }

    std::size_t low = pos + 1;
    std::size_t high = end;
    std::size_t fitEnd = pos;
    while(low <= high) {
      const std::size_t mid = low + (high - low) / 2;
      if(measuredWidth(text.substr(pos, mid - pos), measure) <= width) {
        fitEnd = mid;
        low = mid + 1;
      } else {
        if(mid == 0) break;
        high = mid - 1;
      }
    }

    std::size_t breakAfter = std::numeric_limits<std::size_t>::max();
    for(std::size_t i = fitEnd; i > pos; --i) {
      if(isWrapSpace(text[i - 1])) {
        breakAfter = i;
        break;
      }
    }

    std::size_t rowEnd = fitEnd;
    if(breakAfter != std::numeric_limits<std::size_t>::max() && breakAfter > pos) rowEnd = breakAfter;
    if(rowEnd <= pos) rowEnd = std::min(pos + 1, end);
    rowEnd = snapToCodepoint(text, rowEnd, pos, end);
    pushRow(rows, pos, rowEnd);
    pos = rowEnd;
  }
}

// Every logical line that starts at or after `from` and ends at or before `to`,
// wrapped in order. `from` must be the start of a logical line and `to` its
// end -- which is what `softWrapUpdate` widens the edit to before calling.
void wrapRegion(std::vector<SoftWrapRow>& rows, std::string_view text, std::size_t from,
                std::size_t to, int width, const MeasureText& measure) {
  std::size_t lineStart = from;
  for(std::size_t i = from; i < to; ++i) {
    if(text[i] == '\n') {
      wrapLogicalLine(rows, text, lineStart, i, width, measure);
      lineStart = i + 1;
    }
  }
  wrapLogicalLine(rows, text, lineStart, to, width, measure);
}

// Whether the two rows are consecutive rows of the *same* logical line, which
// is the case exactly when no byte sits between them: a line break is a byte,
// so rows either side of one are a byte apart.
bool sameLogicalLine(const SoftWrapRow& before, const SoftWrapRow& after) {
  return before.end == after.start;
}

}

void softWrapInto(std::vector<SoftWrapRow>& rows, std::string_view text, int width,
                  const MeasureText& measure) {
  rows.clear();
  rows.reserve(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1);
  perf::addCounter(perf::CounterId::EditorSoftWrapRebuilds);
  perf::addCounter(perf::CounterId::EditorSoftWrapBytes, text.size());
  wrapRegion(rows, text, 0, text.size(), width, measure);
  if(rows.empty()) rows.push_back({0, 0});
  perf::addCounter(perf::CounterId::EditorSoftWrapRowsLaid, rows.size());
}

std::vector<SoftWrapRow> softWrap(std::string_view text, int width, const MeasureText& measure) {
  std::vector<SoftWrapRow> rows;
  softWrapInto(rows, text, width, measure);
  return rows;
}

std::size_t softWrapUpdate(std::vector<SoftWrapRow>& rows, SoftWrapScratch& scratch,
                           std::string_view text, const TextEdit& edit, int width,
                           const MeasureText& measure) {
  // Rows partition the whole buffer, so the last one ends at its size -- which
  // makes "do these rows describe the buffer this edit came from" an O(1)
  // question about lengths. It is not the whole question: two buffers of the
  // same length wrap differently, and nothing here can see that. The caller's
  // revision stamps are what answer *that*, and this is the cheap half that
  // catches a caller which has not got them right.
  const std::size_t shift = edit.newEnd - edit.oldEnd;
  if(rows.empty() || rows.back().end + shift != text.size() || edit.oldEnd < edit.start ||
     edit.newEnd < edit.start) {
    softWrapInto(rows, text, width, measure);
    return rows.size();
  }

  // The first row the edit can reach, then back to the start of its logical
  // line. `rows` is sorted by `end`, and every row of a line but the first is
  // preceded by one ending where it starts.
  std::size_t first = static_cast<std::size_t>(
    std::lower_bound(rows.begin(), rows.end(), edit.start,
                     [](const SoftWrapRow& row, std::size_t offset) { return row.end < offset; }) -
    rows.begin());
  if(first >= rows.size()) first = rows.size() - 1;
  while(first > 0 && sameLogicalLine(rows[first - 1], rows[first])) --first;

  // The last row it can reach, then forward to the end of *its* logical line.
  std::size_t last = first;
  while(last + 1 < rows.size() && rows[last].end < edit.oldEnd) ++last;
  while(last + 1 < rows.size() && sameLogicalLine(rows[last], rows[last + 1])) ++last;

  // `shift` is unsigned and a deletion makes it wrap, which is the arithmetic
  // that is wanted: modular addition of the wrapped value is exactly
  // subtraction, and every sum it appears in is a buffer offset the widening
  // above has already put on the safe side of the edit.
  const std::size_t from = rows[first].start;
  const std::size_t oldTo = rows[last].end;
  const std::size_t to = oldTo + shift;

  std::vector<SoftWrapRow>& relaid = scratch.relaid;
  relaid.clear();
  wrapRegion(relaid, text, from, to, width, measure);

  const std::size_t was = last - first + 1;
  const std::size_t now = relaid.size();
  perf::addCounter(perf::CounterId::EditorSoftWrapUpdates);
  perf::addCounter(perf::CounterId::EditorSoftWrapBytes, to - from);
  perf::addCounter(perf::CounterId::EditorSoftWrapRowsLaid, now);
  perf::addCounter(perf::CounterId::EditorSoftWrapRowsShifted, rows.size() - last - 1);

  // The common case by a wide margin -- a typed character rewraps one line into
  // the same number of rows -- and it is the one that needs no move at all.
  if(now == was) {
    std::copy(relaid.begin(), relaid.end(), rows.begin() + static_cast<std::ptrdiff_t>(first));
  } else {
    const auto at = rows.begin() + static_cast<std::ptrdiff_t>(first);
    rows.erase(at, at + static_cast<std::ptrdiff_t>(was));
    rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(first), relaid.begin(), relaid.end());
  }

  // The tail is the same rows, further along by the edit's delta. An addition
  // per row rather than a measurement per row, which is the whole point.
  for(std::size_t i = first + now; i < rows.size(); ++i) {
    rows[i].start += shift;
    rows[i].end += shift;
  }
  if(rows.empty()) rows.push_back({0, 0});
  return now;
}

int rowForOffset(const std::vector<SoftWrapRow>& rows, std::size_t offset) {
  if(rows.empty()) return 0;
  const auto found = std::lower_bound(rows.begin(), rows.end(), offset, [](const SoftWrapRow& row, std::size_t value) {
    return row.end < value;
  });
  if(found == rows.end()) return static_cast<int>(rows.size() - 1);
  return static_cast<int>(std::distance(rows.begin(), found));
}

std::size_t offsetForRowX(std::string_view buffer, const SoftWrapRow& row, float x,
                          const MeasureText& measure) {
  if(x <= 0.0f) return row.start;
  const std::string_view line = textIn(buffer, row);
  if(line.empty()) return row.start;

  std::size_t low = 0;
  std::size_t high = line.size();
  while(low < high) {
    const std::size_t mid = low + (high - low) / 2;
    const float w = static_cast<float>(measuredWidth(line.substr(0, mid), measure));
    if(w < x) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  const std::size_t after = std::min(low, line.size());
  const std::size_t before = after == 0 ? 0 : after - 1;
  const float beforeW = static_cast<float>(measuredWidth(line.substr(0, before), measure));
  const float afterW = static_cast<float>(measuredWidth(line.substr(0, after), measure));
  const std::size_t best =
    util::boundaryAtOrBefore(line, std::abs(afterW - x) <= std::abs(beforeW - x) ? after : before);
  return std::min(row.start + best, row.end);
}

}
