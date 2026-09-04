#include "core/platform/DurableFile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>

namespace microcore::platform {
namespace {

static bool fsyncDirectory(const std::filesystem::path& dir) {
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if(fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
}

static bool writeAll(int fd, std::string_view contents) {
  const char* data = contents.data();
  std::size_t remaining = contents.size();
  while(remaining > 0) {
    const ssize_t written = ::write(fd, data, remaining);
    if(written < 0) {
      if(errno == EINTR) continue;
      return false;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
  return true;
}

// The mode and ownership of the file about to be replaced, so the replacement
// can wear them. A rename installs a brand new inode: without this every save
// reset the note to whatever the umask says, which silently opened up a note
// the user had deliberately made private.
struct Ownership {
  bool valid = false;
  mode_t mode = 0;
  uid_t uid = 0;
  gid_t gid = 0;
};

static Ownership ownershipOf(const std::filesystem::path& path) {
  struct stat status {};
  if(::stat(path.c_str(), &status) != 0) return {};
  return Ownership {true, static_cast<mode_t>(status.st_mode & 07777), status.st_uid,
                    status.st_gid};
}

static void applyOwnership(const std::filesystem::path& path, const Ownership& ownership) {
  if(!ownership.valid) return;
  // Ownership before mode: chown clears setuid and setgid on Linux, so applying
  // it second would undo them.
  const bool owned = ::chown(path.c_str(), ownership.uid, ownership.gid) == 0;
  mode_t mode = ownership.mode;
  // An unprivileged save of a file it does not own gets EPERM here, which is
  // expected and not fatal -- but the file is now owned by the saving user, and
  // re-applying setuid or setgid in that state would manufacture a set-id file
  // under the *wrong* owner. Those two bits are dropped rather than moved.
  if(!owned) mode &= static_cast<mode_t>(~(S_ISUID | S_ISGID));
  ::chmod(path.c_str(), mode);
}

// The end of the symlink chain starting at `path`, or `path` itself when it is
// not a link.
//
// `weakly_canonical` cannot answer this: for `link -> missing.md` it stops at
// the last existing prefix -- the link's own parent -- and appends the link's
// name lexically, handing back the link node. Writing there destroys the link.
// Reading the link and resolving it against its own parent gets the intended
// target whether or not that target exists yet.
static std::filesystem::path resolveSymlink(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::path current = path;
  // Bounded so a cycle (`a -> b -> a`) cannot spin. Forty hops is past any
  // legitimate chain and is roughly the kernel's own limit.
  for(int hops = 0; hops < 40; ++hops) {
    // Keyed on is_symlink rather than on the error: a dangling link's target
    // reports not_found *and* sets the error, and that target is exactly where
    // the write belongs.
    if(!std::filesystem::is_symlink(std::filesystem::symlink_status(current, error))) {
      return current;
    }
    error.clear();
    const auto target = std::filesystem::read_symlink(current, error);
    if(error || target.empty()) return path;
    current = (target.is_absolute() ? target : current.parent_path() / target).lexically_normal();
  }
  return path;
}

// A staging path beside `path`, unique to this process and this call.
//
// A shared fixed suffix lets two writers of the same file corrupt each other:
// one's O_TRUNC zeroes the other's in-flight temp, and what lands is a
// truncated file rather than either version. Per-process and per-call keeps
// each writer's staging private right up to the rename, which degrades
// concurrent writers to last-writer-wins.
static std::filesystem::path temporaryPathFor(const std::filesystem::path& path) {
  static std::atomic<std::uint64_t> counter {0};
  std::string name = ".";
  name += path.filename().string();
  name += kTemporaryWriteMarker;
  name += std::to_string(::getpid());
  name += '.';
  name += std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
  return path.parent_path() / name;
}

// The shared body of both public overloads. `pieces` is written back to back.
static bool writePieces(const std::filesystem::path& path, const std::string_view* pieces,
                        std::size_t count) {
  // The link is followed first, so everything below -- the parent directory, the
  // ownership snapshot, the staging path and the rename -- is about the file the
  // link points at rather than about the link.
  const auto target = resolveSymlink(path);

  std::error_code error;
  const auto parent = target.parent_path();
  if(!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if(error) return false;
  }

  const Ownership ownership = ownershipOf(target);
  const auto temp = temporaryPathFor(target);
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if(fd < 0) return false;

  bool ok = true;
  for(std::size_t i = 0; ok && i < count; ++i) ok = writeAll(fd, pieces[i]);
  ok = ok && ::fsync(fd) == 0;
  if(::close(fd) != 0) ok = false;
  if(!ok) {
    std::filesystem::remove(temp, error);
    return false;
  }

  // 0600 while it is being written, so a half-written note is never readable by
  // anyone the finished one would not be; the replaced file's own mode goes on
  // just before it takes its place. A brand new file gets the umask's answer.
  if(ownership.valid) {
    applyOwnership(temp, ownership);
  } else {
    mode_t mask = ::umask(0);
    ::umask(mask);
    ::chmod(temp.c_str(), static_cast<mode_t>(0666 & ~mask));
  }

  std::filesystem::rename(temp, target, error);
  if(error) {
    std::filesystem::remove(temp, error);
    return false;
  }
  // The bytes are already durable; this is what makes the *rename* durable, so
  // a crash cannot leave the directory still pointing at the previous file.
  return fsyncDirectory(parent.empty() ? std::filesystem::path(".") : parent);
}

}

FileSignature statFile(const std::filesystem::path& path) {
  struct stat status {};
  if(::stat(path.c_str(), &status) != 0) {
    FileSignature signature;
    // ENOENT and ENOTDIR are "there is nothing there", which is a fact. Anything
    // else is "I could not find out", which is not the same thing and must not
    // be allowed to compare equal to a later answer.
    signature.error = errno != ENOENT && errno != ENOTDIR;
    return signature;
  }
  FileSignature signature;
  signature.exists = true;
  signature.mtimeNanos = static_cast<std::uint64_t>(status.st_mtim.tv_sec) * 1'000'000'000ull +
                         static_cast<std::uint64_t>(status.st_mtim.tv_nsec);
  signature.size = static_cast<std::uint64_t>(status.st_size);
  return signature;
}

bool writeFileDurably(const std::filesystem::path& path, std::string_view contents) {
  return writePieces(path, &contents, 1);
}

bool writeFileDurably(const std::filesystem::path& path,
                      std::initializer_list<std::string_view> pieces) {
  return writePieces(path, pieces.begin(), pieces.size());
}

bool removeFileDurably(const std::filesystem::path& path) {
  std::error_code error;
  // Through the error code rather than a preceding `exists()`: the two-call form
  // was a stat on every clear, and it answered a question the remove answers
  // anyway. A file that is already gone is the outcome this asked for.
  if(!std::filesystem::remove(path, error) && !error) return true;
  if(error) return false;
  return fsyncDirectory(path.parent_path());
}

bool isTemporaryWriteName(std::string_view fileName) {
  return fileName.starts_with('.') &&
         fileName.find(kTemporaryWriteMarker) != std::string_view::npos;
}

}
