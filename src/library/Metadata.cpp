#include "library/Metadata.h"

#include <atomic>
#include <chrono>
#include <random>
#include <sstream>

namespace micronotes::library {

std::string generateNoteId() {
  // Wall-clock time keeps ids roughly sortable; a process-unique random salt
  // prevents collisions between separate runs (steady_clock resets per process).
  static const unsigned long long salt = std::random_device {}();
  static std::atomic<unsigned long long> counter {0};
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();
  std::ostringstream out;
  out << "n" << std::hex << now << "-" << salt << "-"
      << counter.fetch_add(1, std::memory_order_relaxed);
  return out.str();
}

std::string metadataHeader(const NoteMetadata& metadata) {
  std::string out = "---\n";
  out += "id: " + metadata.id + "\n";
  out += "title: " + metadata.title + "\n";
  out += "tags:";
  for(const auto& tag : metadata.tags) out += " " + tag;
  out += "\n---\n\n";
  return out;
}

namespace {

// Find the closing front-matter fence: a line that is exactly "---". Avoids
// matching "----" or "---foo" that may appear inside the body.
std::size_t findClosingFence(std::string_view markdown) {
  std::size_t from = 4;
  while(true) {
    const auto pos = markdown.find("\n---", from);
    if(pos == std::string_view::npos) return std::string_view::npos;
    const auto after = pos + 4;
    if(after == markdown.size() || markdown[after] == '\n') return pos;
    from = pos + 1;
  }
}

}

NoteMetadata parseMetadata(std::string_view markdown) {
  NoteMetadata metadata;
  if(!markdown.starts_with("---\n")) return metadata;
  const auto end = findClosingFence(markdown);
  if(end == std::string_view::npos) return metadata;
  std::string header(markdown.substr(4, end - 4));
  std::istringstream lines(header);
  std::string line;
  while(std::getline(lines, line)) {
    if(line.starts_with("id: ")) {
      metadata.id = line.substr(4);
    } else if(line.starts_with("title: ")) {
      metadata.title = line.substr(7);
    } else if(line.starts_with("tags:")) {
      std::istringstream tags(line.substr(5));
      std::string tag;
      while(tags >> tag) metadata.tags.push_back(tag);
    }
  }
  return metadata;
}

std::string stripMetadataHeader(std::string_view markdown) {
  if(!markdown.starts_with("---\n")) return std::string(markdown);
  const auto end = findClosingFence(markdown);
  if(end == std::string_view::npos) return std::string(markdown);
  auto bodyStart = end + 4;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  return std::string(markdown.substr(bodyStart));
}

}
