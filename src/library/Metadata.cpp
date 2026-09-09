#include "library/Metadata.h"

#include "core/util/StringUtil.h"

#include <set>
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

// The two front-matter shape rules. Declared in the header rather than kept in
// the anonymous namespace below, because whatever reads `NoteMetadata::extra`
// back has to split those lines on the same boundary the parser split them on
// -- see the note there.
bool continuesFrontMatterValue(std::string_view line) {
  return !line.empty() && (line.front() == ' ' || line.front() == '\t' || line.starts_with("- "));
}

std::string frontMatterSequenceItem(std::string_view line) {
  const auto text = util::trim(line);
  if(!text.starts_with("- ")) return {};
  return std::string(util::trim(text.substr(2)));
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


// The one definition, from `core/util/StringUtil.h`. This file used to carry a
// third: leading space and tab, trailing space, tab and CR. The asymmetry was
// not a decision -- it is what a leading `\r` cannot be, since these are lines
// read a line at a time -- so folding it into the shared trim changes nothing
// here and removes a copy that read as though it meant something.
static std::string trim(std::string_view value) {
  return std::string(util::trim(value));
}

static void readTags(NoteMetadata& metadata, std::string_view inlineValue,
                     const std::vector<std::string_view>& lines, std::size_t from, std::size_t to) {
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
    // Split on runs of whitespace, over the value itself. This used to build an
    // `istringstream` around a copy of it and extract a `std::string` per tag.
    const std::string_view rest = value;
    std::size_t i = 0;
    while(i < rest.size()) {
      while(i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
      const std::size_t start = i;
      while(i < rest.size() && rest[i] != ' ' && rest[i] != '\t') ++i;
      if(i > start) metadata.tags.emplace_back(rest.substr(start, i - start));
    }
    return;
  }
  // An empty inline value with `- item` lines under it is YAML's block form,
  // which is what most other editors write and what micronotes used to drop.
  for(std::size_t i = from; i < to; ++i) {
    auto tag = frontMatterSequenceItem(lines[i]);
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
  // The note's own name as its first heading, when that is how the note
  // arrived. Written here rather than left in the body so the body holds the
  // name once; see NoteMetadata::titleHeading for why it is not simply dropped.
  if(metadata.titleHeading) out += "# " + metadata.title + "\n\n";
  return out;
}

NoteMetadata parseMetadata(std::string_view markdown) {
  NoteMetadata metadata;
  if(!markdown.starts_with("---\n")) return metadata;
  const auto end = findClosingFence(markdown);
  if(end == std::string_view::npos) return metadata;
  const std::string_view header = markdown.substr(4, end - 4);

  // Split as views into `markdown`. This used to copy the header out, wrap an
  // `istringstream` round the copy and allocate a `std::string` per line --
  // three allocations plus one per line for every note the library reads, to
  // extract at most four short fields. Only the fields kept allocate now, and
  // `extra` is the only one that keeps a whole line.
  std::vector<std::string_view> lines;
  for(std::size_t at = 0; at <= header.size();) {
    const auto newline = header.find('\n', at);
    const std::size_t stop = newline == std::string_view::npos ? header.size() : newline;
    std::string_view line = header.substr(at, stop - at);
    // A file written on Windows ends its lines with CR; it belongs to no field.
    if(!line.empty() && line.back() == '\r') line.remove_suffix(1);
    lines.push_back(line);
    if(newline == std::string_view::npos) break;
    at = newline + 1;
  }

  for(std::size_t i = 0; i < lines.size();) {
    const std::string_view line = lines[i];
    std::size_t next = i + 1;
    while(next < lines.size() && continuesFrontMatterValue(lines[next])) ++next;
    if(line.starts_with("id: ")) metadata.id = line.substr(4);
    else if(line.starts_with("title: ")) metadata.title = line.substr(7);
    else if(line.starts_with("icon: ")) metadata.icon = trim(line.substr(6));
    else if(line.starts_with("tags:")) readTags(metadata, line.substr(5), lines, i + 1, next);
    else if(!trim(line).empty()) {
      for(std::size_t j = i; j < next; ++j) metadata.extra.emplace_back(lines[j]);
    }
    i = next;
  }
  return metadata;
}

std::size_t titleHeadingLength(std::string_view body, std::string_view title) {
  if(title.empty() || !body.starts_with("# ")) return 0;
  const auto newline = body.find('\n');
  const auto line = body.substr(0, newline == std::string_view::npos ? body.size() : newline);
  if(trim(line.substr(2)) != title) return 0;
  std::size_t length = newline == std::string_view::npos ? body.size() : newline + 1;
  // The blank line that separated the heading from the first real block goes
  // with it, exactly as the blank line under the front matter fence does. Only
  // one: a run of them is spacing the reader asked for, and the block scanner
  // draws it.
  if(length < body.size() && body[length] == '\n') ++length;
  return length;
}

std::size_t metadataHeaderLength(std::string_view markdown) {
  if(!markdown.starts_with("---\n")) return 0;
  const auto end = findClosingFence(markdown);
  if(end == std::string_view::npos) return 0;
  auto bodyStart = end + 4;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  if(bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
  return bodyStart;
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
