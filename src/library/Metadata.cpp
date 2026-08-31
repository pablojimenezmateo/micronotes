#include "library/Metadata.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>

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

std::string fallbackNoteId(std::string_view relativePath) {
  // FNV-1a 64-bit: short, dependency-free, and stable across platforms.
  std::uint64_t hash = 1469598103934665603ull;
  for(const unsigned char c : relativePath) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  std::ostringstream out;
  out << "p" << std::hex << hash;
  return out.str();
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


static std::string trim(std::string_view value) {
  std::size_t from = 0;
  std::size_t to = value.size();
  while(from < to && (value[from] == ' ' || value[from] == '\t')) ++from;
  while(to > from && (value[to - 1] == ' ' || value[to - 1] == '\t' || value[to - 1] == '\r')) --to;
  return std::string(value.substr(from, to - from));
}

// A front-matter entry is a `key:` line plus the lines that continue its value:
// anything indented, and the `- item` lines of a block sequence. Splitting on
// that boundary is what lets an unrecognized key be carried through with its
// whole value, however many lines it spans.
static bool continuesValue(std::string_view line) {
  return !line.empty() && (line.front() == ' ' || line.front() == '\t' || line.starts_with("- "));
}

static std::string sequenceItem(std::string_view line) {
  const auto text = trim(line);
  if(!text.starts_with("- ")) return {};
  return trim(std::string_view(text).substr(2));
}

static void readTags(NoteMetadata& metadata, std::string_view inlineValue,
                     const std::vector<std::string>& lines, std::size_t from, std::size_t to) {
  const auto value = trim(inlineValue);
  if(value.starts_with("[")) {
    metadata.tagForm = NoteMetadata::TagForm::Flow;
    const auto close = value.find(']');
    std::string_view body = std::string_view(value).substr(1, close == std::string::npos ? std::string::npos : close - 1);
    std::string tag;
    for(const char c : body) {
      if(c == ',' || c == ' ' || c == '\t') {
        if(!tag.empty()) metadata.tags.push_back(tag);
        tag.clear();
        continue;
      }
      tag.push_back(c);
    }
    if(!tag.empty()) metadata.tags.push_back(tag);
    return;
  }
  if(!value.empty()) {
    metadata.tagForm = NoteMetadata::TagForm::Inline;
    std::istringstream tags {value};
    std::string tag;
    while(tags >> tag) metadata.tags.push_back(tag);
    return;
  }
  // An empty inline value with `- item` lines under it is YAML's block form,
  // which is what most other editors write and what micronotes used to drop.
  for(std::size_t i = from; i < to; ++i) {
    auto tag = sequenceItem(lines[i]);
    if(!tag.empty()) {
      metadata.tagForm = NoteMetadata::TagForm::Block;
      metadata.tags.push_back(std::move(tag));
    }
  }
}

static std::string writeTags(const NoteMetadata& metadata) {
  if(metadata.tagForm == NoteMetadata::TagForm::Block && !metadata.tags.empty()) {
    std::string out = "tags:\n";
    for(const auto& tag : metadata.tags) out += "  - " + tag + "\n";
    return out;
  }
  if(metadata.tagForm == NoteMetadata::TagForm::Flow) {
    std::string out = "tags: [";
    for(std::size_t i = 0; i < metadata.tags.size(); ++i) {
      if(i > 0) out += ", ";
      out += metadata.tags[i];
    }
    return out + "]\n";
  }
  std::string out = "tags:";
  for(const auto& tag : metadata.tags) out += " " + tag;
  return out + "\n";
}

}

std::string metadataHeader(const NoteMetadata& metadata) {
  std::string out = "---\n";
  out += "id: " + metadata.id + "\n";
  out += "title: " + metadata.title + "\n";
  // Omitted rather than written empty: a note without an icon should read the
  // same as one micronotes never touched.
  if(!metadata.icon.empty()) out += "icon: " + metadata.icon + "\n";
  // Omitted when there are none, for the same reason as the icon: a note that
  // arrived without a `tags:` key must not grow an empty one the first time
  // micronotes saves it.
  if(!metadata.tags.empty()) out += writeTags(metadata);
  // Verbatim, in the order they were read, so a round trip through micronotes
  // leaves another tool's keys exactly as it left them.
  for(const auto& line : metadata.extra) {
    out += line;
    out += "\n";
  }
  out += "---\n\n";
  return out;
}

NoteMetadata parseMetadata(std::string_view markdown) {
  NoteMetadata metadata;
  if(!markdown.starts_with("---\n")) return metadata;
  const auto end = findClosingFence(markdown);
  if(end == std::string_view::npos) return metadata;
  std::string header(markdown.substr(4, end - 4));
  std::vector<std::string> lines;
  {
    std::istringstream in {header};
    std::string line;
    while(std::getline(in, line)) {
      if(!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(std::move(line));
    }
  }

  for(std::size_t i = 0; i < lines.size();) {
    const std::string& line = lines[i];
    std::size_t next = i + 1;
    while(next < lines.size() && continuesValue(lines[next])) ++next;
    if(line.starts_with("id: ")) metadata.id = line.substr(4);
    else if(line.starts_with("title: ")) metadata.title = line.substr(7);
    else if(line.starts_with("icon: ")) metadata.icon = trim(std::string_view(line).substr(6));
    else if(line.starts_with("tags:")) readTags(metadata, std::string_view(line).substr(5), lines, i + 1, next);
    else if(!trim(line).empty()) {
      for(std::size_t j = i; j < next; ++j) metadata.extra.push_back(lines[j]);
    }
    i = next;
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
