#include "ui/TextUtil.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace micronotes::ui {

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
