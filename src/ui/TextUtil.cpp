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


namespace {

// The code point boundary at or before `i`. A cut only ever lands on one of
// these, which is what keeps a truncation from handing the renderer half of a
// UTF-8 sequence. Continuation bytes are `10xxxxxx` and a sequence is at most
// four bytes long, so this steps back three times at most -- which is why the
// three searches below index *bytes* and snap, rather than first building a
// vector of every boundary in the string. That vector was eight bytes per byte
// of the line, allocated and filled to be probed a handful of times.
std::size_t snapBack(std::string_view value, std::size_t i) {
  if(i >= value.size()) return value.size();
  while(i > 0 && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80) --i;
  return i;
}

// The boundary after `i`, which is where a cut goes when one code point has to
// be kept whether it fits or not.
std::size_t nextStop(std::string_view value, std::size_t i) {
  if(i >= value.size()) return value.size();
  ++i;
  while(i < value.size() && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80) ++i;
  return i;
}

// The largest offset in [low, high) that `fits`, where `fits` is monotone --
// true up to a crossover and false from there on -- `low` is taken to fit
// whether it does or not, and `high` is known not to.
//
// The bracket is opened at `guess` and galloped outwards rather than started at
// the midpoint, and that is the whole point of this function. Every probe here
// is a substring nothing has measured before -- a unique piece of a unique line
// -- so the measure cache cannot serve one and each is a real shaping pass; and
// a bisection from the top spends its first probes measuring hundreds of bytes
// to discover that hundreds of bytes do not fit. The caller has already
// measured the whole string for its early-out, so `width / bytes` is an advance
// per byte and the answer is a division away, to within a few characters for
// proportional text. Two or three probes near the answer's own length replace
// eighteen starting at the line's.
//
// The guarantee is unchanged: a cut is only ever returned once it has been
// *measured* to fit. A bad guess costs probes, never correctness.
template <typename Fits>
std::size_t largestFitting(std::size_t low, std::size_t high, std::size_t guess, const Fits& fits) {
  if(low + 1 >= high) return low;
  guess = std::clamp(guess, low + 1, high - 1);
  std::size_t step = 1;
  if(fits(guess)) {
    low = guess;
    while(low + step < high) {
      if(!fits(low + step)) {
        high = low + step;
        break;
      }
      low += step;
      step *= 2;
    }
  } else {
    high = guess;
    while(high - low > step) {
      if(fits(high - step)) {
        low = high - step;
        break;
      }
      high -= step;
      step *= 2;
    }
  }
  while(low + 1 < high) {
    const std::size_t mid = low + (high - low) / 2;
    if(fits(mid)) low = mid;
    else high = mid;
  }
  return low;
}

// The smallest offset in (low, high] that `fits`, where `fits` is false below
// the crossover and true from there on, `high` is taken to fit and `low` is
// known not to. That is `largestFitting` read backwards, so it is that function
// with the index reflected through the bracket rather than a second copy of a
// search this delicate.
template <typename Fits>
std::size_t smallestFitting(std::size_t low, std::size_t high, std::size_t guess, const Fits& fits) {
  const auto reflect = [&](std::size_t i) { return low + high - i; };
  const auto reflected = [&](std::size_t i) { return fits(reflect(i)); };
  return reflect(largestFitting(low, high, reflect(std::clamp(guess, low, high)), reflected));
}

}

std::string ellipsizeToFit(std::string value, int maxWidth,
                           const std::function<int(std::string_view)>& measure) {
  if(maxWidth <= 0) return "";
  const int full = measure(value);
  if(full <= maxWidth) return value;

  static constexpr std::string_view kEllipsis = "...";
  std::string candidate;
  const auto fitsAt = [&](std::size_t cut) {
    candidate.assign(value, 0, snapBack(value, cut));
    candidate.append(kEllipsis);
    return measure(candidate) <= maxWidth;
  };
  // `full` is the width of every byte of the string, so the room left once the
  // ellipsis has taken its share converts straight back into a byte count. The
  // ellipsis is the same three bytes at every call site, so its measurement is
  // the one probe here the cache does serve.
  const int room = maxWidth - measure(kEllipsis);
  const std::size_t guess =
      room <= 0 || full <= 0
          ? 0
          : static_cast<std::size_t>(static_cast<double>(value.size()) * room / full);
  // A cut of zero means not even one character and the ellipsis fit, and the
  // answer is the ellipsis alone -- which is what an empty prefix produces.
  const std::size_t cut = snapBack(value, largestFitting(0, value.size(), guess, fitsAt));
  return value.substr(0, cut) + std::string(kEllipsis);
}

SnippetWindow snippetAroundMatch(std::string_view line, std::size_t matchStart, std::size_t matchLength,
                                 int maxWidth, const std::function<int(std::string_view)>& measure) {
  static constexpr std::string_view kEllipsis = "...";

  SnippetWindow out;
  matchStart = std::min(matchStart, line.size());
  matchLength = std::min(matchLength, line.size() - matchStart);
  if(maxWidth <= 0) return out;

  // The common case, and the cheap one: the whole line fits and there is
  // nothing to decide.
  const int full = measure(line);
  if(full <= maxWidth) {
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
  const std::size_t matchEnd = matchStart + matchLength;
  std::string probe;
  const auto survives = [&](std::size_t from) {
    from = snapBack(line, from);
    probe.clear();
    if(from > 0) probe += kEllipsis;
    probe.append(line.substr(from, matchEnd - from));
    probe.append(kEllipsis);
    return measure(probe) <= maxWidth;
  };

  // Keeping the whole head is the best answer when it is available: a line
  // read from its start needs no explaining.
  std::size_t from = 0;
  if(!survives(0)) {
    // The least head that can go is none of it; the most is all of it up to the
    // match, which keeps no run-up at all. Monotone in between: a later cut is
    // a shorter string, so once one fits every later one does.
    const std::size_t atMatch = snapBack(line, matchStart);
    if(!survives(atMatch)) {
      // The match is wider than the column by itself. Nothing can show all of
      // it, so show its start and let the trim clip the rest.
      from = atMatch;
    } else {
      // Two ellipses and the match have to fit together, and `full` says what a
      // byte of this line costs, so the run-up that is affordable is a division
      // rather than a search from the top.
      const int room = maxWidth - 2 * measure(kEllipsis);
      const std::size_t affordable =
          room <= 0 || full <= 0
              ? 0
              : static_cast<std::size_t>(static_cast<double>(line.size()) * room / full);
      const std::size_t guess = matchEnd > affordable ? matchEnd - affordable : 0;
      from = snapBack(line, smallestFitting(0, atMatch, guess, survives));
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
  if(maxWidth <= 0) return value.size();
  const int full = measure(value);
  if(full <= maxWidth) return value.size();

  const auto fitsAt = [&](std::size_t cut) {
    return measure(value.substr(0, snapBack(value, cut))) <= maxWidth;
  };
  // One code point is taken whether it fits or not, because it has nowhere else
  // to go, so that boundary is the floor of the search rather than a case in it.
  const std::size_t first = nextStop(value, 0);
  const std::size_t guess =
      static_cast<std::size_t>(static_cast<double>(value.size()) * maxWidth / full);
  return snapBack(value, largestFitting(first, value.size(), guess, fitsAt));
}

}
