#include "library/Library.h"

#include "platform/PathUtils.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
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

static std::filesystem::path trashRoot() {
  if(const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
    return std::filesystem::path(xdgDataHome) / "Trash";
  }
  if(const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".local" / "share" / "Trash";
  }
  return std::filesystem::temp_directory_path() / "micronotes-trash";
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

static std::filesystem::path uniqueTrashPath(const std::filesystem::path& desired) {
  if(!std::filesystem::exists(desired)) return desired;
  const auto parent = desired.parent_path();
  const auto stem = desired.stem().string();
  const auto ext = desired.extension().string();
  int suffix = 2;
  while(true) {
    auto candidate = parent / (stem + "-" + std::to_string(suffix++) + ext);
    if(!std::filesystem::exists(candidate)) return candidate;
  }
}

static void writeTrashInfo(const std::filesystem::path& infoPath, const std::filesystem::path& originalPath) {
  std::ofstream out(infoPath);
  out << "[Trash Info]\n";
  out << "Path=" << originalPath.string() << "\n";
  out << "DeletionDate=" << timestampNow() << "\n";
}

static void moveToTrash(const std::filesystem::path& path) {
  if(path.empty() || !std::filesystem::exists(path)) return;
  const auto root = trashRoot();
  const auto files = root / "files";
  const auto info = root / "info";
  std::filesystem::create_directories(files);
  std::filesystem::create_directories(info);
  const auto target = uniqueTrashPath(files / path.filename());
  try {
    std::filesystem::rename(path, target);
  } catch(const std::filesystem::filesystem_error&) {
    if(std::filesystem::is_directory(path)) {
      std::filesystem::copy(path, target, std::filesystem::copy_options::recursive);
      std::filesystem::remove_all(path);
    } else {
      std::filesystem::copy_file(path, target);
      std::filesystem::remove(path);
    }
  }
  writeTrashInfo(info / (target.filename().string() + ".trashinfo"), path);
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
  std::ofstream out(path);
  out << metadataHeader(metadata);
  out << body;
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

void Library::saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  std::ofstream out(safePath);
  out << metadataHeader(metadata) << body;
}

void Library::updateTags(const std::filesystem::path& path, const std::vector<std::string>& tags) const {
  auto note = loadNote(path);
  note.metadata.tags = tags;
  saveNote(path, note.metadata, note.body);
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
  saveNote(target, note.metadata, note.body);
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
  std::vector<std::filesystem::path> attachmentDirs;
  for(const auto& path : noteFiles()) {
    const auto relative = path.lexically_relative(safePath);
    if(relative.empty() || relative.native().starts_with("..")) continue;
    const auto metadata = loadNoteMetadata(path);
    if(!metadata.id.empty()) attachmentDirs.push_back(root_ / ".micronotes" / "attachments" / metadata.id);
  }
  moveToTrash(safePath);
  for(const auto& attachmentDir : attachmentDirs) moveToTrash(attachmentDir);
}

void Library::deleteNote(const std::filesystem::path& path) const {
  const auto safePath = platform::normalizeInsideRoot(root_, path);
  const auto metadata = loadNoteMetadata(safePath);
  moveToTrash(safePath);
  if(!metadata.id.empty()) moveToTrash(root_ / ".micronotes" / "attachments" / metadata.id);
}

std::vector<std::filesystem::path> Library::noteFiles() const {
  std::vector<std::filesystem::path> files;
  if(!std::filesystem::exists(root_)) return files;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(root_)) {
    if(!entry.is_regular_file()) continue;
    const auto path = entry.path();
    if(path.extension() != ".md") continue;
    if(path.string().find((root_ / ".micronotes").string()) == 0) continue;
    files.push_back(path);
  }
  return files;
}

}
