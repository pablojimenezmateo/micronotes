#include "ui/TextUtil.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <sstream>

namespace micronotes::ui {

std::string displayPath(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  const char* home = std::getenv("HOME");
  if(!home || !*home) return text;
  const std::string prefix(home);
  if(text.rfind(prefix, 0) != 0) return text;
  if(text.size() == prefix.size()) return "~";
  if(text[prefix.size()] != '/') return text;
  return "~" + text.substr(prefix.size());
}

std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for(const char c : text) {
    if(c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  lines.push_back(current);
  return lines;
}

std::string ellipsize(std::string text, std::size_t limit) {
  if(text.size() <= limit) return text;
  if(limit <= 3) return text.substr(0, limit);
  return text.substr(0, limit - 3) + "...";
}

bool isRemoteTarget(std::string_view target) {
  return target.starts_with("http://") || target.starts_with("https://");
}

std::string fileNameForMime(std::string_view mime) {
  if(mime == "image/png") return "clipboard.png";
  if(mime == "image/jpeg" || mime == "image/jpg") return "clipboard.jpg";
  if(mime == "image/bmp") return "clipboard.bmp";
  if(mime == "image/webp") return "clipboard.webp";
  return "clipboard-image";
}

std::vector<std::string> splitTags(std::string_view value) {
  std::vector<std::string> tags;
  std::set<std::string> seen;
  std::istringstream in {std::string(value)};
  std::string tag;
  while(in >> tag) {
    if(!tag.empty() && tag.front() == '#') tag.erase(tag.begin());
    if(tag.empty() || seen.contains(tag)) continue;
    seen.insert(tag);
    tags.push_back(tag);
  }
  return tags;
}

std::string joinTags(const std::vector<std::string>& tags) {
  std::string out;
  for(const auto& tag : tags) {
    if(!out.empty()) out += " ";
    out += tag;
  }
  return out;
}


// Every code point boundary in `value`, including both ends. Cutting a string
// only ever at one of these is what keeps a truncation from handing the
// renderer half of a UTF-8 sequence.
static void codePointStops(std::string_view value, std::vector<std::size_t>* out) {
  out->clear();
  out->reserve(value.size() + 1);
  for(std::size_t i = 0; i <= value.size();) {
    out->push_back(i);
    if(i == value.size()) break;
    ++i;
    while(i < value.size() && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80) ++i;
  }
}

std::string ellipsizeToFit(std::string value, int maxWidth,
                           const std::function<int(std::string_view)>& measure) {
  if(maxWidth <= 0) return "";
  if(measure(value) <= maxWidth) return value;

  std::vector<std::size_t> stops;
  codePointStops(value, &stops);

  static constexpr std::string_view kEllipsis = "...";
  std::string candidate;
  const auto fitsAt = [&](std::size_t stop) {
    candidate.assign(value, 0, stops[stop]);
    candidate.append(kEllipsis);
    return measure(candidate) <= maxWidth;
  };
  std::size_t fits = 0;
  std::size_t over = stops.size() - 1;  // the whole string, already known not to
  while(fits + 1 < over) {
    const std::size_t mid = fits + (over - fits) / 2;
    if(fitsAt(mid)) fits = mid;
    else over = mid;
  }
  // `fits == 0` means not even one character and the ellipsis fit, and the
  // answer is the ellipsis alone -- which is what an empty prefix produces.
  return value.substr(0, stops[fits]) + std::string(kEllipsis);
}

namespace {

// The stop at or before `offset`, so a cut lands on a code point boundary.
std::size_t stopAtOrBefore(const std::vector<std::size_t>& stops, std::size_t offset) {
  std::size_t at = 0;
  while(at + 1 < stops.size() && stops[at + 1] <= offset) ++at;
  return at;
}

}

// TD-2: about 0.25 ms a call. Both bisections here start from the whole string
// and work down, and every probe is a substring nothing has measured before, so
// the measure cache never helps and the early probes are hundreds of bytes long.
// The caller now asks only for the rows it is drawing, which is what made this
// affordable; docs/tech-debt.md has the fix if it stops being.
SnippetWindow snippetAroundMatch(std::string_view line, std::size_t matchStart, std::size_t matchLength,
                                 int maxWidth, const std::function<int(std::string_view)>& measure) {
  static constexpr std::string_view kEllipsis = "...";

  SnippetWindow out;
  matchStart = std::min(matchStart, line.size());
  matchLength = std::min(matchLength, line.size() - matchStart);
  if(maxWidth <= 0) return out;

  // The common case, and the cheap one: the whole line fits and there is
  // nothing to decide.
  if(measure(line) <= maxWidth) {
    out.text = std::string(line);
    out.start = matchStart;
    out.length = matchLength;
    return out;
  }

  // It does not fit, so something has to go. Whatever goes, the match has to
  // stay, and the tail is trimmed by ellipsizeToFit below -- which keeps the
  // longest prefix that fits *with an ellipsis appended*. So the question is
  // how much of the head to give up for the head ellipsis, the match, and that
  // trailing one to fit together; anything less and the tail trim eats into the
  // match, which is the bug this function exists to make unwriteable.
  std::vector<std::size_t> stops;
  codePointStops(line, &stops);
  const std::size_t matchEnd = matchStart + matchLength;
  const auto survives = [&](std::size_t from) {
    std::string probe;
    if(from > 0) probe += kEllipsis;
    probe.append(line.substr(from, matchEnd - from));
    probe.append(kEllipsis);
    return measure(probe) <= maxWidth;
  };

  // Keeping the whole head is the best answer when it is available: a line
  // read from its start needs no explaining.
  std::size_t from = 0;
  if(!survives(0)) {
    // Bisect for the smallest head cut that does fit. Monotone: a later cut is
    // a shorter string, so once one fits every later one does. `over` starts at
    // the stop just tested and known not to fit, `fits` at the match's own
    // start, which keeps no run-up at all.
    std::size_t over = 0;
    std::size_t fits = stopAtOrBefore(stops, matchStart);
    if(!survives(stops[fits])) {
      // The match is wider than the column by itself. Nothing can show all of
      // it, so show its start and let the trim clip the rest.
      from = stops[fits];
    } else {
      while(over + 1 < fits) {
        const std::size_t mid = over + (fits - over) / 2;
        if(survives(stops[mid])) fits = mid;
        else over = mid;
      }
      from = stops[fits];
    }
  }

  std::string text;
  std::size_t start = matchStart - from;
  if(from > 0) {
    text += kEllipsis;
    start += kEllipsis.size();
  }
  text.append(line.substr(from));

  out.text = ellipsizeToFit(std::move(text), maxWidth, measure);
  out.start = std::min(start, out.text.size());
  out.length = std::min(matchLength, out.text.size() - out.start);
  return out;
}

std::size_t breakToFit(std::string_view value, int maxWidth,
                       const std::function<int(std::string_view)>& measure) {
  if(value.empty()) return 0;
  if(maxWidth <= 0 || measure(value) <= maxWidth) return value.size();

  std::vector<std::size_t> stops;
  codePointStops(value, &stops);
  // `fits` is the last stop known to fit, `over` the first known not to. The
  // whole string is already known not to, and one code point is taken to fit
  // whether it does or not, because it has nowhere else to go.
  std::size_t fits = 1;
  std::size_t over = stops.size() - 1;
  while(fits + 1 < over) {
    const std::size_t mid = fits + (over - fits) / 2;
    if(measure(value.substr(0, stops[mid])) <= maxWidth) fits = mid;
    else over = mid;
  }
  return stops[fits];
}

}
