#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Reading the source tree back, for the tests whose subject is the tree rather
// than anything it compiles to.
//
// These four were written once and then copied as the architecture tests split
// into the rules about *where code lives*, the rules about *one list rather
// than two*, and the render caches that were never architecture at all. A
// helper that reads a file is exactly the kind of thing three files each end up
// with their own slightly different version of.

namespace micronotes::tests {

inline std::filesystem::path repoRoot() {
  return std::filesystem::path(MICRONOTES_SOURCE_DIR);
}

inline std::string readText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

inline std::vector<std::filesystem::path> sourceFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  if(!std::filesystem::exists(root)) return files;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if(!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension();
    if(extension == ".cpp" || extension == ".h") files.push_back(entry.path());
  }
  return files;
}

// The text with every `//` comment taken out, line by line. A rule read off a
// declaration has to be read off the declarations: this tree writes more prose
// than code, and prose about a method parses as one.
inline std::string withoutLineComments(const std::string& text) {
  std::string out;
  std::size_t at = 0;
  while(at <= text.size()) {
    const auto end = text.find('\n', at);
    const auto stop = end == std::string::npos ? text.size() : end;
    const auto line = text.substr(at, stop - at);
    out += line.substr(0, line.find("//"));
    out.push_back('\n');
    if(end == std::string::npos) break;
    at = end + 1;
  }
  return out;
}

inline int lineCount(const std::filesystem::path& path) {
  const std::string text = readText(path);
  return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

}
