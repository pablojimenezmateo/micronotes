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

// The whole file, in one read into one right-sized buffer.
//
// This used to stream `in.rdbuf()` into an `ostringstream` and return
// `buffer.str()`. That is three costs stacked on a path a library refresh walks
// once per note: the stream buffer grows by doubling, so a 200 KB note is a
// dozen reallocations and copies; `str()` on an lvalue hands back a *copy* of
// what it grew; and an `ostringstream` drags a locale and a sentry along for a
// job that is one `read`.
static std::string readAll(std::istream& in) {
  std::string out;
  if(!in) return out;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if(size < 0) return out;
  in.seekg(0, std::ios::beg);
  out.resize(static_cast<std::size_t>(size));
  if(!out.empty()) in.read(out.data(), static_cast<std::streamsize>(out.size()));
  // A file that shrank between the two seeks reads short; the count is what
  // actually arrived, not what the size said.
  out.resize(static_cast<std::size_t>(in.gcount()));
  return out;
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

// A free name inside the trash for `original`. `reserved` holds the names
// already handed out in this batch but not yet on disk, which the filesystem
// check alone cannot see -- a folder delete files the folder and every
// attachment directory under it before a single one of them has moved.
static std::string uniqueTrashName(const std::filesystem::path& files,
                                   const std::filesystem::path& original,
                                   const std::vector<std::string>& reserved) {
  const auto stem = original.stem().string();
  const auto ext = original.extension().string();
  const auto taken = [&](const std::string& candidate) {
    return std::filesystem::exists(files / candidate) ||
           std::find(reserved.begin(), reserved.end(), candidate) != reserved.end();
  };
  std::string candidate = stem + ext;
  int suffix = 2;
  while(taken(candidate)) candidate = stem + "-" + std::to_string(suffix++) + ext;
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

// Moves `path` into `files/name`. The name is reserved separately, because the
// index entry naming it has to be on disk *before* the file moves -- see
// `appendTrashEntries`.
static bool moveIntoTrashAs(const std::filesystem::path& root, const std::filesystem::path& path,
                            const std::string& name) {
  if(name.empty() || path.empty() || !std::filesystem::exists(path)) return false;
  const auto target = trashFiles(root) / name;
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

static std::string trashEntryLine(const TrashEntry& entry) {
  return escapeField(entry.name) + '\t' + escapeField(entry.originalRelative) + '\t' +
         escapeField(entry.title) + '\t' + escapeField(entry.deletedAt) + '\t' +
         escapeField(entry.attachmentName) + '\t' +
         escapeField(entry.attachmentOriginalRelative) + '\n';
}

// Adds `entries` to the trash index, durably, in one write.
//
// This was an `ofstream` in append mode with no flush and no fsync, and the
// index is the *only* record of where a deleted note came from. An append a
// crash loses leaves the file sitting in `trash/files` with nothing naming it:
// `trashEntries()` cannot list it, so the person who deleted it cannot get it
// back from inside the app, and the note is gone as far as they can tell.
//
// A read-modify-write of the whole file instead. It is a line per deletion and
// deletions are rare, so that is affordable -- and it is what makes the
// ordering below possible, which is the part that actually matters.
static bool appendTrashEntries(const std::filesystem::path& root,
                               const std::vector<TrashEntry>& entries) {
  if(entries.empty()) return true;
  std::string index;
  if(std::ifstream in(trashIndex(root)); in) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    index = buffer.str();
  }
  if(!index.empty() && index.back() != '\n') index.push_back('\n');
  for(const auto& entry : entries) index += trashEntryLine(entry);
  return platform::writeFileDurably(trashIndex(root), index);
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

std::filesystem::path filesRootOf(const std::filesystem::path& relative) {
  std::filesystem::path walk;
  for(const auto& part : relative) {
    // A trailing slash leaves an empty last component and `.` names nothing;
    // neither is a directory the rule could apply to.
    if(part.empty() || part == ".") continue;
    walk /= part;
    if(part == kFilesDirName) return walk;
  }
  return {};
}

bool insideFilesDir(const std::filesystem::path& relative) {
  return !filesRootOf(relative).empty();
}

bool isFilesDir(const std::filesystem::path& relative) {
  // The last real component is `files`, and nothing before it is. Spelled as a
  // walk rather than as `filesRootOf(relative) == relative` so a trailing slash
  // or a `.` in the path does not make the same directory two different ones.
  bool sawFiles = false;
  for(const auto& part : relative) {
    if(part.empty() || part == ".") continue;
    if(sawFiles) return false;
    if(part == kFilesDirName) sawFiles = true;
  }
  return sawFiles;
}

Library::Library(std::filesystem::path root)
  : root_(std::move(root)), safeRoot_(root_) {}

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
  const auto path = platform::uniquePath(safeRoot_.normalize(notePath(metadata.title)));
  const auto header = metadataHeader(metadata);
  if(!platform::writeFileDurably(path, {header, body})) {
    throw std::runtime_error("failed to create note");
  }
  return path;
}

LoadedNote Library::loadNote(const std::filesystem::path& path) const {
  const auto safePath = safeRoot_.normalize(path);
  std::ifstream in(safePath, std::ios::binary);
  std::string text = readAll(in);
  auto metadata = parseMetadata(text);
  // Both prefixes are measured before either is removed, so the buffer that was
  // just read becomes the body by one erase rather than being cut out into a
  // fresh string -- which, for the one caller that matters, was a second copy of
  // every byte of every note a library refresh reads.
  std::size_t bodyStart = metadataHeaderLength(text);
  const std::string_view body = std::string_view(text).substr(bodyStart);
  // A `# <name>` first line is the note's own header rather than the first
  // thing it says, so it comes off with the front matter and goes back on with
  // it. The name is read the way every other reader of the library reads it:
  // the front matter's title, or the file's stem when it carries none.
  const auto title = metadata.title.empty() ? safePath.stem().string() : metadata.title;
  if(const std::size_t heading = titleHeadingLength(body, title); heading > 0) {
    metadata.titleHeading = true;
    bodyStart += heading;
  }
  text.erase(0, bodyStart);
  return {std::move(metadata), std::move(text)};
}

NoteMetadata Library::loadNoteMetadata(const std::filesystem::path& path) const {
  const auto safePath = safeRoot_.normalize(path);
  std::ifstream in(safePath);
  return parseMetadata(readMetadataHeader(in));
}

bool Library::saveNote(const std::filesystem::path& path, const NoteMetadata& metadata, std::string_view body) const {
  const auto safePath = safeRoot_.normalize(path);
  // Two pieces rather than one concatenated buffer: the body is the whole note,
  // and gluing a hundred-byte header to the front of it cost an allocation and
  // a copy of every byte of the note on every autosave.
  const auto header = metadataHeader(metadata);
  return platform::writeFileDurably(safePath, {header, body});
}

std::string Library::preserveExternalVersion(const std::filesystem::path& path) const {
  const auto safePath = safeRoot_.normalize(path);
  if(!std::filesystem::exists(safePath)) return {};
  auto note = loadNote(safePath);
  const auto stem = safePath.stem().string();
  const auto title = (note.metadata.title.empty() ? stem : note.metadata.title) +
                     " (external change " + timestampNow() + ")";
  note.metadata.id = generateNoteId();
  note.metadata.title = title;
  const auto target = platform::uniquePath(
    safePath.parent_path() / (platform::sanitizeFileStem(title) + ".md"));
  if(!saveNote(target, note.metadata, note.body)) return {};
  return target.filename().string();
}

std::filesystem::path Library::createFolder(const std::filesystem::path& relativeFolder) const {
  const auto target = safeRoot_.normalize(root_ / relativeFolder);
  std::filesystem::create_directories(target);
  return target;
}

std::filesystem::path Library::saveNoteAs(const std::filesystem::path& path,
                                          const NoteMetadata& metadata,
                                          std::string_view body) const {
  const auto safePath = safeRoot_.normalize(path);
  const auto target = platform::uniquePath(
    safeRoot_.normalize(safePath.parent_path() /
                                           (platform::sanitizeFileStem(metadata.title) + ".md")),
    safePath);
  if(!saveNote(target, metadata, body)) return {};
  if(target != safePath) {
    std::error_code error;
    std::filesystem::remove(safePath, error);
  }
  return target;
}

std::filesystem::path Library::renameNote(const std::filesystem::path& path, const std::string& newTitle) const {
  const auto safePath = safeRoot_.normalize(path);
  auto note = loadNote(safePath);
  note.metadata.title = newTitle;
  const auto target = saveNoteAs(safePath, note.metadata, note.body);
  return target.empty() ? safePath : target;
}

std::filesystem::path Library::moveNote(const std::filesystem::path& path, const std::filesystem::path& relativeFolder) const {
  const auto safePath = safeRoot_.normalize(path);
  const auto targetDir = safeRoot_.normalize(root_ / relativeFolder);
  std::filesystem::create_directories(targetDir);
  const auto target = platform::uniquePath(safeRoot_.normalize(targetDir / safePath.filename()), safePath);
  if(target == safePath) return target;
  std::filesystem::rename(safePath, target);
  return target;
}

std::filesystem::path Library::renameFolder(const std::filesystem::path& relativeFolder, const std::filesystem::path& newRelativeFolder) const {
  const auto safePath = safeRoot_.normalize(root_ / relativeFolder);
  const auto target = safeRoot_.normalize(root_ / newRelativeFolder);
  if(safePath == root_ || target == root_) return safePath;
  std::filesystem::create_directories(target.parent_path());
  std::filesystem::rename(safePath, target);
  return target;
}

void Library::deleteFolder(const std::filesystem::path& relativeFolder) const {
  const auto safePath = safeRoot_.normalize(root_ / relativeFolder);
  if(safePath == root_) return;
  // The whole folder goes as one entry, so restoring it brings back everything
  // that was inside. Attachments live outside it and are filed alongside.
  if(!std::filesystem::exists(safePath)) return;
  std::vector<std::filesystem::path> attachmentDirs;
  for(const auto& path : noteFiles()) {
    const auto relative = path.lexically_relative(safePath);
    if(relative.empty() || relative.native().starts_with("..")) continue;
    const auto metadata = loadNoteMetadata(path);
    if(metadata.id.empty()) continue;
    const auto dir = root_ / ".micronotes" / "attachments" / metadata.id;
    if(std::filesystem::exists(dir)) attachmentDirs.push_back(dir);
  }

  const auto files = trashFiles(root_);
  std::filesystem::create_directories(files);
  const auto deletedAt = timestampNow();
  std::vector<std::string> reserved;
  std::vector<TrashEntry> entries;

  TrashEntry entry;
  entry.name = uniqueTrashName(files, safePath, reserved);
  reserved.push_back(entry.name);
  entry.originalRelative = safePath.lexically_relative(root_);
  entry.title = safePath.filename().string();
  entry.deletedAt = deletedAt;
  entries.push_back(entry);
  for(const auto& attachmentDir : attachmentDirs) {
    TrashEntry attachment;
    attachment.name = uniqueTrashName(files, attachmentDir, reserved);
    reserved.push_back(attachment.name);
    attachment.originalRelative = attachmentDir.lexically_relative(root_);
    attachment.deletedAt = deletedAt;
    entries.push_back(std::move(attachment));
  }

  // One durable write for the folder and every attachment directory under it,
  // rather than one per entry. A folder of a hundred notes used to be a hundred
  // appends; it is now a hundred lines in a single write -- and, for the reason
  // `deleteNote` gives, the write comes first.
  if(!appendTrashEntries(root_, entries)) return;
  if(!moveIntoTrashAs(root_, safePath, entries.front().name)) return;
  for(std::size_t i = 0; i < attachmentDirs.size(); ++i) {
    moveIntoTrashAs(root_, attachmentDirs[i], entries[i + 1].name);
  }
}

void Library::deleteNote(const std::filesystem::path& path) const {
  const auto safePath = safeRoot_.normalize(path);
  if(!std::filesystem::exists(safePath)) return;
  const auto metadata = loadNoteMetadata(safePath);
  const auto attachmentDir = metadata.id.empty() ? std::filesystem::path {}
                                                 : root_ / ".micronotes" / "attachments" / metadata.id;
  const auto files = trashFiles(root_);
  std::filesystem::create_directories(files);

  std::vector<std::string> reserved;
  TrashEntry entry;
  entry.originalRelative = safePath.lexically_relative(root_);
  entry.title = metadata.title.empty() ? safePath.stem().string() : metadata.title;
  entry.deletedAt = timestampNow();
  entry.name = uniqueTrashName(files, safePath, reserved);
  reserved.push_back(entry.name);
  const bool hasAttachments = !attachmentDir.empty() && std::filesystem::exists(attachmentDir);
  if(hasAttachments) {
    entry.attachmentName = uniqueTrashName(files, attachmentDir, reserved);
    entry.attachmentOriginalRelative = attachmentDir.lexically_relative(root_);
  }

  // The index entry lands *before* the file moves, and this ordering is the
  // whole point. A crash after the move and before the index left a note in the
  // trash that nothing named and nobody could restore. A crash the other way
  // round leaves an index line for a file that never arrived -- and
  // `trashEntries()` already skips an entry whose file is not there, so it is
  // history rather than a broken offer.
  if(!appendTrashEntries(root_, {entry})) return;
  if(!moveIntoTrashAs(root_, safePath, entry.name)) return;
  if(hasAttachments) moveIntoTrashAs(root_, attachmentDir, entry.attachmentName);
}

// Every line of the trash index, parsed. Both readers of it -- the list a person
// is offered and the restore that has to rewrite the file -- walked it
// themselves, and each had its own copy of which field is which and of the
// optional attachment columns that came later.
//
// The two differ in what they *keep*, not in how they read: the offer drops
// entries whose file has gone and entries with no title, and the restore needs
// every row because it rewrites the whole file. So the filtering stays with
// each caller and only the parse is shared.
std::vector<TrashEntry> Library::readTrashIndex() const {
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
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<TrashEntry> Library::trashEntries() const {
  auto entries = readTrashIndex();
  const auto gone = [&](const TrashEntry& entry) {
    // An entry whose file is gone -- emptied by hand, or already restored -- is
    // history, not an offer. Attachment directories are filed on their own so a
    // folder restore can find them, but they are not something to offer a
    // person, and they are the entries with no title.
    return entry.title.empty() || !std::filesystem::exists(trashFiles(root_) / entry.name);
  };
  entries.erase(std::remove_if(entries.begin(), entries.end(), gone), entries.end());
  // Newest first: the thing just deleted is the thing most likely wanted back.
  std::reverse(entries.begin(), entries.end());
  return entries;
}

bool Library::restoreFromTrash(const std::string& name) const {
  const std::vector<TrashEntry> all = readTrashIndex();
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto& entry) { return entry.name == name; });
  if(found == all.end()) return false;

  const auto put = [&](const std::string& trashName, const std::filesystem::path& relative) {
    if(trashName.empty()) return false;
    const auto source = trashFiles(root_) / trashName;
    if(!std::filesystem::exists(source)) return false;
    auto target = safeRoot_.normalize(root_ / relative);
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

void Library::walk(std::vector<std::filesystem::directory_entry>* filesOut,
                   std::vector<std::filesystem::path>* directoriesOut,
                   std::vector<CompanionEntry>* companionsOut) const {
  perf::addCounter(perf::CounterId::LibraryNoteFilesCalls);
  std::vector<std::filesystem::directory_entry> discard;
  std::vector<std::filesystem::directory_entry>& files = filesOut ? *filesOut : discard;
  files.clear();
  if(directoriesOut) directoriesOut->clear();
  if(companionsOut) companionsOut->clear();
  if(!std::filesystem::exists(root_)) return;

  // The state directory holds the sqlite index, its WAL, and every attachment.
  // The walk used to descend into all of it and then discard the results by
  // comparing path prefixes -- which also meant building `root_ / kAppDotDir`
  // and two std::strings for *every* entry in the tree. Prune the subtree
  // instead: on a library with attachments it is by far the largest part of it.
  const std::filesystem::path stateDir = root_ / microcore::kAppDotDir;

  std::error_code error;
  std::filesystem::recursive_directory_iterator it(
    root_, std::filesystem::directory_options::skip_permission_denied, error);
  if(error) return;
  const std::filesystem::recursive_directory_iterator end;
  // The depth at which the walk entered a files directory, or -1 outside one.
  // Everything deeper than it is a companion; the first entry back at or above
  // it is out again. By depth rather than by inspecting each path's components:
  // that is O(1) per entry on a walk whose whole cost is the entries.
  int filesDepth = -1;
  for(; it != end; it.increment(error)) {
    if(error) break;
    perf::addCounter(perf::CounterId::LibraryDirectoryEntriesVisited);
    if(filesDepth >= 0 && it.depth() <= filesDepth) filesDepth = -1;
    const bool directory = it->is_directory(error);
    if(directory && filesDepth < 0 && it->path() == stateDir) {
      it.disable_recursion_pending();
      continue;
    }
    if(filesDepth < 0 && directory && it->path().filename() == kFilesDirName) {
      filesDepth = it.depth();
    }
    if(filesDepth >= 0) {
      perf::addCounter(perf::CounterId::LibraryCompanionEntriesVisited);
      if(!companionsOut) continue;
      if(!directory && !it->is_regular_file(error)) continue;
      if(!directory && platform::isTemporaryWriteName(it->path().filename().native())) continue;
      auto relative = it->path().lexically_relative(root_);
      auto folder = relative.parent_path();
      companionsOut->push_back({std::move(relative), std::move(folder), directory});
      continue;
    }
    if(directory) {
      if(directoriesOut) directoriesOut->push_back(it->path().lexically_relative(root_));
      continue;
    }
    if(!it->is_regular_file(error)) continue;
    if(it->path().extension() != ".md") continue;
    // A durable write stages beside its target, so a save in flight puts a file
    // in this tree for the length of one rename. Named rather than left to the
    // extension test above, which happens to reject it today only because the
    // staging counter is the last component of the name.
    if(platform::isTemporaryWriteName(it->path().filename().native())) continue;
    files.push_back(*it);
  }
}

std::vector<std::filesystem::path> Library::noteFiles() const {
  std::vector<std::filesystem::directory_entry> entries;
  walk(&entries, nullptr);
  std::vector<std::filesystem::path> files;
  files.reserve(entries.size());
  for(const auto& entry : entries) files.push_back(entry.path());
  return files;
}

std::vector<CompanionEntry> Library::walkFilesDir(const std::filesystem::path& relativeFilesDir) const {
  std::vector<CompanionEntry> out;
  if(!isFilesDir(relativeFilesDir)) return out;
  const auto dir = safeRoot_.normalize(root_ / relativeFilesDir);
  std::error_code error;
  if(!std::filesystem::is_directory(dir, error)) return out;
  out.push_back({relativeFilesDir, relativeFilesDir.parent_path(), true});
  std::filesystem::recursive_directory_iterator it(
    dir, std::filesystem::directory_options::skip_permission_denied, error);
  if(error) return out;
  const std::filesystem::recursive_directory_iterator end;
  for(; it != end; it.increment(error)) {
    if(error) break;
    perf::addCounter(perf::CounterId::LibraryCompanionEntriesVisited);
    const bool directory = it->is_directory(error);
    if(!directory && !it->is_regular_file(error)) continue;
    if(!directory && platform::isTemporaryWriteName(it->path().filename().native())) continue;
    auto relative = it->path().lexically_relative(root_);
    auto folder = relative.parent_path();
    out.push_back({std::move(relative), std::move(folder), directory});
  }
  return out;
}

std::filesystem::path Library::moveCompanion(const std::filesystem::path& relative,
                                             const std::filesystem::path& newRelative) const {
  // Both ends inside a files directory, and neither end the directory itself.
  // A companion moved out of a files area would become a note, a notebook or
  // an invisible file, and the `files` directory renamed would take every file
  // in it out of the tree at once.
  if(!insideFilesDir(relative) || isFilesDir(relative)) return {};
  if(!insideFilesDir(newRelative) || isFilesDir(newRelative)) return {};
  const auto source = safeRoot_.normalize(root_ / relative);
  std::error_code error;
  if(!std::filesystem::exists(source, error)) return {};
  auto target = safeRoot_.normalize(root_ / newRelative);
  // A directory cannot be moved into itself: the rename would take it, and
  // everything under it, out of reach. From the target's parent, so that a
  // rename to the name it already has is a no-op rather than a refusal.
  for(auto walk = target.parent_path(); !walk.empty() && walk != walk.root_path(); walk = walk.parent_path()) {
    if(walk == source) return {};
  }
  std::filesystem::create_directories(target.parent_path(), error);
  target = platform::uniquePath(target, source);
  if(target == source) return target;
  std::filesystem::rename(source, target, error);
  return error ? std::filesystem::path {} : target;
}

bool Library::deleteCompanion(const std::filesystem::path& relative) const {
  if(!insideFilesDir(relative)) return false;
  const auto safePath = safeRoot_.normalize(root_ / relative);
  if(!std::filesystem::exists(safePath)) return false;
  const auto files = trashFiles(root_);
  std::filesystem::create_directories(files);
  TrashEntry entry;
  entry.originalRelative = safePath.lexically_relative(root_);
  // The file's own name, extension included: a companion has no title but the
  // one the reader gave the file, and the restore list should show it as the
  // tree did.
  entry.title = safePath.filename().string();
  entry.deletedAt = timestampNow();
  entry.name = uniqueTrashName(files, safePath, {});
  // Index line first, then the move, for the reason `deleteNote` gives.
  if(!appendTrashEntries(root_, {entry})) return false;
  return moveIntoTrashAs(root_, safePath, entry.name);
}

}
