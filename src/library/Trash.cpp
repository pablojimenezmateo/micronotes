#include "library/Trash.h"

#include "CoreAliases.h"
#include "library/StatePaths.h"

#include "core/platform/DurableFile.h"
#include "core/platform/PathUtils.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace micronotes::library {
namespace {

// A tab-separated line per entry, so a name with a space in it -- which is most
// of them -- needs no quoting. Tabs and newlines cannot appear in a path that
// reached here, since every one of them was written by the library itself, so
// the escape is a substitution rather than an encoding: there is nothing to
// decode on the way back.
std::string escapeField(std::string value) {
  for(char& c : value) {
    if(c == '\t' || c == '\n') c = ' ';
  }
  return value;
}

std::string escapeField(const std::filesystem::path& value) {
  return escapeField(value.generic_string());
}

std::vector<std::string> splitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t from = 0;
  while(true) {
    const auto tab = line.find('\t', from);
    if(tab == std::string::npos) {
      fields.push_back(line.substr(from));
      return fields;
    }
    fields.push_back(line.substr(from, tab - from));
    from = tab + 1;
  }
}

}

Trash::Trash(const std::filesystem::path& root)
  : root_(root), files_(trashFilesDir(root)), index_(trashIndexPath(root)) {}

void Trash::ensure() const {
  std::filesystem::create_directories(files_);
}

std::string Trash::reserveNameFor(const std::filesystem::path& original,
                                  std::vector<std::string>& reserved) const {
  const auto stem = original.stem().string();
  const auto ext = original.extension().string();
  const auto taken = [&](const std::string& candidate) {
    return std::filesystem::exists(files_ / candidate) ||
           std::find(reserved.begin(), reserved.end(), candidate) != reserved.end();
  };
  std::string candidate = stem + ext;
  int suffix = 2;
  while(taken(candidate)) candidate = stem + "-" + std::to_string(suffix++) + ext;
  reserved.push_back(candidate);
  return candidate;
}

std::string Trash::entryLine(const TrashEntry& entry) {
  return escapeField(entry.name) + '\t' + escapeField(entry.originalRelative) + '\t' +
         escapeField(entry.title) + '\t' + escapeField(entry.deletedAt) + '\t' +
         escapeField(entry.attachmentName) + '\t' +
         escapeField(entry.attachmentOriginalRelative) + '\n';
}

// A read-modify-write of the whole file rather than an append. It is a line per
// deletion and deletions are rare, so that is affordable -- and it is what
// makes one durable write serve a whole folder's worth of entries.
bool Trash::append(const std::vector<TrashEntry>& entries) const {
  if(entries.empty()) return true;
  std::string index;
  if(std::ifstream in(index_); in) {
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if(size > 0) {
      in.seekg(0, std::ios::beg);
      index.resize(static_cast<std::size_t>(size));
      in.read(index.data(), static_cast<std::streamsize>(index.size()));
      index.resize(static_cast<std::size_t>(in.gcount()));
    }
  }
  if(!index.empty() && index.back() != '\n') index.push_back('\n');
  for(const auto& entry : entries) index += entryLine(entry);
  return platform::writeFileDurably(index_, index);
}

bool Trash::fileAs(const std::filesystem::path& path, const std::string& name) const {
  if(name.empty() || path.empty() || !std::filesystem::exists(path)) return false;
  const auto target = files_ / name;
  std::error_code ec;
  std::filesystem::rename(path, target, ec);
  if(ec) {
    // A rename across devices fails; a copy and remove says the same thing.
    if(std::filesystem::is_directory(path)) {
      std::filesystem::copy(path, target, std::filesystem::copy_options::recursive, ec);
      std::filesystem::remove_all(path, ec);
    } else {
      std::filesystem::copy_file(path, target, ec);
      std::filesystem::remove(path, ec);
    }
    if(ec) return false;
  }
  return true;
}

// Both readers of the index walked it themselves, and each had its own copy of
// which field is which and of the optional attachment columns that came later.
// They differ in what they *keep*, not in how they read, so the filtering stays
// with `entries()` and only the parse is shared.
std::vector<TrashEntry> Trash::readIndex() const {
  std::vector<TrashEntry> entries;
  std::ifstream in(index_);
  std::string line;
  while(std::getline(in, line)) {
    if(line.empty()) continue;
    const auto fields = splitFields(line);
    if(fields.size() < 4) continue;
    TrashEntry entry;
    entry.name = fields[0];
    entry.originalRelative = fields[1];
    entry.title = fields[2];
    entry.deletedAt = fields[3];
    if(fields.size() > 4) entry.attachmentName = fields[4];
    if(fields.size() > 5) entry.attachmentOriginalRelative = fields[5];
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<TrashEntry> Trash::entries() const {
  auto all = readIndex();
  const auto gone = [&](const TrashEntry& entry) {
    return entry.title.empty() || !std::filesystem::exists(files_ / entry.name);
  };
  all.erase(std::remove_if(all.begin(), all.end(), gone), all.end());
  std::reverse(all.begin(), all.end());
  return all;
}

bool Trash::restore(const std::string& name, const platform::SafeRoot& safeRoot) const {
  const std::vector<TrashEntry> all = readIndex();
  const auto found = std::find_if(all.begin(), all.end(),
                                  [&](const auto& entry) { return entry.name == name; });
  if(found == all.end()) return false;

  const auto put = [&](const std::string& trashName, const std::filesystem::path& relative) {
    if(trashName.empty()) return false;
    const auto source = files_ / trashName;
    if(!std::filesystem::exists(source)) return false;
    auto target = safeRoot.normalize(root_ / relative);
    // Something may have taken the name back in the meantime; the restored copy
    // gets a new one rather than overwriting it. A restored *folder* has no
    // extension and must not be given one, which is why `uniquePath` invents
    // none.
    target = platform::uniquePath(target);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::rename(source, target, ec);
    return !static_cast<bool>(ec);
  };
  if(!put(found->name, found->originalRelative)) return false;
  put(found->attachmentName, found->attachmentOriginalRelative);
  // Also restore the attachment directories a deleted folder left behind, which
  // were filed as entries of their own.
  if(std::filesystem::is_directory(root_ / found->originalRelative)) {
    for(const auto& entry : all) {
      if(entry.title.empty() && entry.deletedAt == found->deletedAt) {
        put(entry.name, entry.originalRelative);
      }
    }
  }

  // Through `entryLine`, so a column added to the format reaches the rewrite as
  // well as the append. This was the six fields written out a second time.
  std::string rewritten;
  for(const auto& entry : all) {
    if(!std::filesystem::exists(files_ / entry.name)) continue;
    rewritten += entryLine(entry);
  }
  platform::writeFileDurably(index_, rewritten);
  return true;
}

}
