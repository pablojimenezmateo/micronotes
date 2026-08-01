#include "CoreAliases.h"
#include "library/Library.h"

#include "core/AppIdentity.h"
#include "core/perf/PerformanceCounters.h"

#include "core/platform/PathUtils.h"
#include "core/platform/DurableFile.h"

#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <vector>

namespace micronotes::library {
namespace {

static std::filesystem::path uniqueMarkdownPath(const std::filesystem::path& desired, const std::filesystem::path& current = {}) {
  if(!std::filesystem::exists(desired) || (!current.empty() && std::filesystem::equivalent(desired, current))) return desired;
  const auto parent = desired.parent_path();
  const auto stem = desired.stem().string();
  const auto ext = desired.extension().empty() ? ".md" : desired.extension().string();
  int suffix = 2;
  while(true) {
    auto candidate = parent / (stem + "-" + std::to_string(suffix++) + ext);
    if(!std::filesystem::exists(candidate)) return candidate;
  }
}

static std::string readAll(std::ifstream& in) {
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

static std::string readMetadataHeader(std::ifstream& in) {
  std::string line;
  if(!std::getline(in, line) || line != "---") return {};
  std::string header = "---\n";
  while(std::getline(in, line)) {
    header += line;
    header += "\n";
    if(line == "---") return header;
  }
  return {};
}

static std::string timestampNow() {
  const auto now = std::time(nullptr);
  std::tm local {};
#if defined(__unix__)
  localtime_r(&now, &local);
#else
  local = *std::localtime(&now);
#endif
  char buffer[32] {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
  return buffer;
}

// The trash lives inside the library rather than in the desktop's, because
// restoring has to work from inside micronotes and no system trash can be read
// back portably. It stays under `.micronotes/`, which `noteFiles()` skips, so a
// deleted note vanishes from the library the moment it is moved.
static std::filesystem::path trashFiles(const std::filesystem::path& root) {
  return root / ".micronotes" / "trash" / "files";
}

static std::filesystem::path trashIndex(const std::filesystem::path& root) {
  return root / ".micronotes" / "trash" / "index";
}

static std::string uniqueTrashName(const std::filesystem::path& files, const std::filesystem::path& original) {
  const auto stem = original.stem().string();
  const auto ext = original.extension().string();
  std::string candidate = stem + ext;
  int suffix = 2;
  while(std::filesystem::exists(files / candidate)) {
    candidate = stem + "-" + std::to_string(suffix++) + ext;
  }
  return candidate;
}

// A tab-separated line per entry. Tabs and newlines cannot appear in a name
// that reached here, since every path was written by the library itself.
static std::string escapeField(const std::filesystem::path& value) {
  std::string out = value.generic_string();
  for(char& c : out) {
    if(c == '\t' || c == '\n') c = ' ';
  }
  return out;
}

static std::string escapeField(std::string value) {
  for(char& c : value) {
    if(c == '\t' || c == '\n') c = ' ';
  }
  return value;
}

// Moves `path` into the trash and returns the name it was filed under, or an
// empty string when there was nothing to move.
static std::string moveIntoTrash(const std::filesystem::path& root, const std::filesystem::path& path) {
  if(path.empty() || !std::filesystem::exists(path)) return {};
  const auto files = trashFiles(root);
  std::filesystem::create_directories(files);
  const auto name = uniqueTrashName(files, path);
  const auto target = files / name;
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
    if(ec) return {};
  }
  return name;
}

static void appendTrashEntry(const std::filesystem::path& root, const TrashEntry& entry) {
  std::filesystem::create_directories(trashIndex(root).parent_path());
  std::ofstream out(trashIndex(root), std::ios::app);
  out << escapeField(entry.name) << '\t'
      << escapeField(entry.originalRelative) << '\t'
      << escapeField(entry.title) << '\t'
      << escapeField(entry.deletedAt) << '\t'
      << escapeField(entry.attachmentName) << '\t'
      << escapeField(entry.attachmentOriginalRelative) << '\n';
}

static std::vector<std::string> splitFields(const std::string& line) {
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

Library::Library(std::filesystem::path root) : root_(std::move(root)) {}

const std::filesystem::path& Library::root() const {
  return root_;
}

void Library::ensureLayout() const {
  std::filesystem::create_directories(root_);
  std::filesystem::create_directories(root_ / ".micronotes" / "attachments");
}

std::filesystem::path Library::notePath(const std::string& title) const {
  return root_ / (platform::sanitizeFileStem(title) + ".md");
}

std::filesystem::path Library::createNote(const NoteMetadata& metadata, std::string_view body) const {
  ensureLayout();
  const auto path = uniqueMarkdownPath(platform::normalizeInsideRoot(root_, notePath(metadata.title)));
  if(!platform::writeFileDurably(path, metadataHeader(metadata) + std::string(body))) {
    throw std::runtime_error("failed to create note");
  }
  return path;
}

LoadedNote Library::loadNote(const std::filesystem::path& path) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  std::ifstream in(safePath);
  const auto markdown = readAll(in);
  return {parseMetadata(markdown), stripMetadataHeader(markdown)};
}

std::string Library::loadNoteBody(const std::filesystem::path& path) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  std::ifstream in(safePath);
  return stripMetadataHeader(readAll(in));
}

NoteMetadata Library::loadNoteMetadata(const std::filesystem::path& path) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  std::ifstream in(safePath);
  return parseMetadata(readMetadataHeader(in));
}

bool Library::saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  return platform::writeFileDurably(safePath, metadataHeader(metadata) + std::string(body));
}

bool Library::updateTags(const std::filesystem::path& path, const std::vector<std::string>& tags) const {
  auto note = loadNote(path);
  note.metadata.tags = tags;
  return saveNote(path, note.metadata, note.body);
}

std::filesystem::path Library::createFolder(const std::filesystem::path& relativeFolder) const {
  const auto target = platform::normalizeInsideRoot(root_, root_ / relativeFolder);
  std::filesystem::create_directories(target);
  return target;
}

std::filesystem::path Library::renameNote(const std::filesystem::path& path, const std::string& newTitle) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  auto note = loadNote(safePath);
  note.metadata.title = newTitle;
  const auto target = uniqueMarkdownPath(platform::normalizeInsideRoot(root_, safePath.parent_path() / (platform::sanitizeFileStem(newTitle) + ".md")), safePath);
  if(!saveNote(target, note.metadata, note.body)) return safePath;
  if(target != safePath) std::filesystem::remove(safePath);
  return target;
}

std::filesystem::path Library::moveNote(const std::filesystem::path& path, const std::filesystem::path& relativeFolder) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  const auto targetDir = platform::normalizeInsideRoot(root_, root_ / relativeFolder);
  std::filesystem::create_directories(targetDir);
  const auto target = uniqueMarkdownPath(platform::normalizeInsideRoot(root_, targetDir / safePath.filename()), safePath);
  if(target == safePath) return target;
  std::filesystem::rename(safePath, target);
  return target;
}

std::filesystem::path Library::renameFolder(const std::filesystem::path& relativeFolder, const std::filesystem::path& newRelativeFolder) const {
  const auto safePath = platform::normalizeInsideRoot(root_, root_ / relativeFolder);
  const auto target = platform::normalizeInsideRoot(root_, root_ / newRelativeFolder);
  if(safePath == root_ || target == root_) return safePath;
  std::filesystem::create_directories(target.parent_path());
  std::filesystem::rename(safePath, target);
  return target;
}

void Library::deleteFolder(const std::filesystem::path& relativeFolder) const {
  const auto safePath = platform::normalizeInsideRoot(root_, root_ / relativeFolder);
  if(safePath == root_) return;
  // The whole folder goes as one entry, so restoring it brings back everything
  // that was inside. Attachments live outside it and are filed alongside.
  std::vector<std::filesystem::path> attachmentDirs;
  for(const auto& path : noteFiles()) {
    const auto relative = path.lexically_relative(safePath);
    if(relative.empty() || relative.native().starts_with("..")) continue;
    const auto metadata = loadNoteMetadata(path);
    if(!metadata.id.empty()) attachmentDirs.push_back(root_ / ".micronotes" / "attachments" / metadata.id);
  }
  TrashEntry entry;
  entry.name = moveIntoTrash(root_, safePath);
  if(entry.name.empty()) return;
  entry.originalRelative = safePath.lexically_relative(root_);
  entry.title = safePath.filename().string();
  entry.deletedAt = timestampNow();
  appendTrashEntry(root_, entry);
  for(const auto& attachmentDir : attachmentDirs) {
    TrashEntry attachment;
    attachment.name = moveIntoTrash(root_, attachmentDir);
    if(attachment.name.empty()) continue;
    attachment.originalRelative = attachmentDir.lexically_relative(root_);
    attachment.deletedAt = entry.deletedAt;
    appendTrashEntry(root_, attachment);
  }
}

void Library::deleteNote(const std::filesystem::path& path) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  const auto metadata = loadNoteMetadata(safePath);
  const auto attachmentDir = metadata.id.empty() ? std::filesystem::path {}
                                                 : root_ / ".micronotes" / "attachments" / metadata.id;
  TrashEntry entry;
  entry.originalRelative = safePath.lexically_relative(root_);
  entry.title = metadata.title.empty() ? safePath.stem().string() : metadata.title;
  entry.deletedAt = timestampNow();
  entry.name = moveIntoTrash(root_, safePath);
  if(entry.name.empty()) return;
  if(!attachmentDir.empty() && std::filesystem::exists(attachmentDir)) {
    entry.attachmentName = moveIntoTrash(root_, attachmentDir);
    entry.attachmentOriginalRelative = attachmentDir.lexically_relative(root_);
  }
  appendTrashEntry(root_, entry);
}

std::vector<TrashEntry> Library::trashEntries() const {
  std::vector<TrashEntry> entries;
  std::ifstream in(trashIndex(root_));
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
    // An entry whose file is gone - emptied by hand, or already restored - is
    // history, not an offer.
    if(!std::filesystem::exists(trashFiles(root_) / entry.name)) continue;
    // Attachment directories are filed on their own so a folder restore can
    // find them, but they are not something to offer a person.
    if(entry.title.empty()) continue;
    entries.push_back(std::move(entry));
  }
  std::reverse(entries.begin(), entries.end());
  return entries;
}

bool Library::restoreFromTrash(const std::string& name) const {
  std::vector<TrashEntry> all;
  {
    std::ifstream in(trashIndex(root_));
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
      all.push_back(std::move(entry));
    }
  }
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto& entry) { return entry.name == name; });
  if(found == all.end()) return false;

  const auto put = [&](const std::string& trashName, const std::filesystem::path& relative) {
    if(trashName.empty()) return false;
    const auto source = trashFiles(root_) / trashName;
    if(!std::filesystem::exists(source)) return false;
    auto target = platform::normalizeInsideRoot(root_, root_ / relative);
    // Something may have taken the name back in the meantime; the restored copy
    // gets a new one rather than overwriting it. Written out here rather than
    // reusing the note path helper, which assumes a `.md` file and would give a
    // restored folder an extension.
    if(std::filesystem::exists(target)) {
      const auto stem = target.stem().string();
      const auto ext = target.extension().string();
      int suffix = 2;
      std::filesystem::path candidate;
      do {
        candidate = target.parent_path() / (stem + "-" + std::to_string(suffix++) + ext);
      } while(std::filesystem::exists(candidate));
      target = candidate;
    }
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
      if(entry.title.empty() && entry.deletedAt == found->deletedAt) put(entry.name, entry.originalRelative);
    }
  }

  std::string rewritten;
  for(const auto& entry : all) {
    if(!std::filesystem::exists(trashFiles(root_) / entry.name)) continue;
    rewritten += escapeField(entry.name) + '\t' + escapeField(entry.originalRelative) + '\t' +
                 escapeField(entry.title) + '\t' + escapeField(entry.deletedAt) + '\t' +
                 escapeField(entry.attachmentName) + '\t' + escapeField(entry.attachmentOriginalRelative) + '\n';
  }
  platform::writeFileDurably(trashIndex(root_), rewritten);
  return true;
}

std::vector<std::filesystem::directory_entry> Library::noteFileEntries() const {
  perf::addCounter(perf::CounterId::LibraryNoteFilesCalls);
  std::vector<std::filesystem::directory_entry> files;
  if(!std::filesystem::exists(root_)) return files;

  // The state directory holds the sqlite index, its WAL, and every attachment.
  // The walk used to descend into all of it and then discard the results by
  // comparing path prefixes -- which also meant building `root_ / kAppDotDir`
  // and two std::strings for *every* entry in the tree. Prune the subtree
  // instead: on a library with attachments it is by far the largest part of it.
  const std::filesystem::path stateDir = root_ / microcore::kAppDotDir;

  std::error_code error;
  std::filesystem::recursive_directory_iterator it(
    root_, std::filesystem::directory_options::skip_permission_denied, error);
  if(error) return files;
  const std::filesystem::recursive_directory_iterator end;
  for(; it != end; it.increment(error)) {
    if(error) break;
    perf::addCounter(perf::CounterId::LibraryDirectoryEntriesVisited);
    if(it->is_directory(error)) {
      if(it->path() == stateDir) it.disable_recursion_pending();
      continue;
    }
    if(!it->is_regular_file(error)) continue;
    if(it->path().extension() != ".md") continue;
    files.push_back(*it);
  }
  return files;
}

std::vector<std::filesystem::path> Library::noteFiles() const {
  const auto entries = noteFileEntries();
  std::vector<std::filesystem::path> files;
  files.reserve(entries.size());
  for(const auto& entry : entries) files.push_back(entry.path());
  return files;
}

}
