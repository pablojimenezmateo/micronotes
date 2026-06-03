#include "library/Metadata.h"

#include <atomic>
#include <chrono>
#include <sstream>
#include <sstream>

namespace micronotes::library {

std::string generateNoteId() {
  static std::atomic<unsigned long long> counter {0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::ostringstream out;
  out << "n" << now << "-" << counter.fetch_add(1, std::memory_order_relaxed);
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

NoteMetadata parseMetadata(std::string_view markdown) {
  NoteMetadata metadata;
  if(!markdown.starts_with("---\n")) return metadata;
  const auto end = markdown.find("\n---", 4);
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
  const auto end = markdown.find("\n---", 4);
  if(end == std::string_view::npos) return std::string(markdown);
  auto bodyStart = end + 4;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  return std::string(markdown.substr(bodyStart));
}

}
