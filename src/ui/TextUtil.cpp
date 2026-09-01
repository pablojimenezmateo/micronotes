#include "ui/TextUtil.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace micronotes::ui {

std::string trimTitle(std::string_view text) {
  std::istringstream lines {std::string(text)};
  std::string line;
  while(std::getline(lines, line)) {
    while(!line.empty() && (line.front() == '#' || std::isspace(static_cast<unsigned char>(line.front())))) {
      line.erase(line.begin());
    }
    if(!line.empty()) return line.substr(0, 60);
  }
  return "Untitled";
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

}
