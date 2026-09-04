#include "core/platform/DirectoryWatcher.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <system_error>

#include "core/platform/DurableFile.h"

namespace microcore::platform {
namespace {

// What is worth waking up for.
//
// `IN_CLOSE_WRITE` rather than `IN_MODIFY`: a program writing a file produces a
// modify event per buffer flush, and the only moment the file is worth reading
// is when the writer has finished with it. `IN_MOVED_TO` is the other half of
// that, and it is the important one -- every careful writer, this app included,
// saves by writing a temp and renaming it into place, so an atomic save arrives
// as a move and never as a write at all.
//
// `IN_EXCL_UNLINK` stops a file that has been deleted but is still held open
// somewhere from continuing to report events under a name nothing resolves to.
constexpr std::uint32_t kMask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE |
                                IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_EXCL_UNLINK;

void closeIfOpen(int& fd) {
  if(fd < 0) return;
  ::close(fd);
  fd = -1;
}

}

DirectoryWatcher::~DirectoryWatcher() {
  stop();
}

void DirectoryWatcher::setWake(WakeFn wake) {
  std::lock_guard<std::mutex> lock(mutex_);
  wake_ = std::move(wake);
}

bool DirectoryWatcher::active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return inotify_ >= 0;
}

std::size_t DirectoryWatcher::watchCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return directories_.size();
}

bool DirectoryWatcher::watch(std::filesystem::path root, std::vector<std::string> ignoredNames) {
  stop();
  std::error_code error;
  if(!std::filesystem::is_directory(root, error)) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  inotify_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if(inotify_ < 0) return false;
  int pipes[2] = {-1, -1};
  if(::pipe2(pipes, O_CLOEXEC) != 0) {
    closeIfOpen(inotify_);
    return false;
  }
  stopRead_ = pipes[0];
  stopWrite_ = pipes[1];

  root_ = std::move(root);
  ignored_ = std::move(ignoredNames);
  changed_.clear();
  rescanRequested_ = false;
  stopping_ = false;
  // Walked here rather than on the worker thread, so that by the time this
  // returns the tree really is being watched. The caller reads the whole
  // library at startup anyway; one more pass over its directories is
  // proportionate, and the alternative is a window in which changes are missed
  // with nothing to say they were.
  if(!addTree(root_)) rescanRequested_ = true;
  worker_ = std::thread([this] { run(); });
  return true;
}

bool DirectoryWatcher::addTree(const std::filesystem::path& dir) {
  if(directories_.size() >= kMaxWatches) return false;
  const int wd = ::inotify_add_watch(inotify_, dir.c_str(), kMask);
  // ENOSPC is the per-user watch limit, which is a property of the machine
  // rather than a bug. Either way the answer is the same: this tree cannot be
  // named change by change, so the caller gets rescan requests instead.
  if(wd < 0) return false;
  directories_[wd] = dir;

  std::error_code error;
  std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, error);
  if(error) return true;
  const std::filesystem::directory_iterator end;
  bool complete = true;
  for(; it != end; it.increment(error)) {
    if(error) break;
    if(!it->is_directory(error)) continue;
    const auto name = it->path().filename().string();
    if(std::find(ignored_.begin(), ignored_.end(), name) != ignored_.end()) continue;
    if(!addTree(it->path())) complete = false;
  }
  return complete;
}

void DirectoryWatcher::requestRescan() {
  rescanRequested_ = true;
}

void DirectoryWatcher::run() {
  std::vector<char> buffer(64 * 1024);
  while(true) {
    int inotify = -1;
    int stopRead = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inotify = inotify_;
      stopRead = stopRead_;
    }
    if(inotify < 0 || stopRead < 0) return;

    pollfd fds[2] = {
      pollfd {inotify, POLLIN, 0},
      pollfd {stopRead, POLLIN, 0},
    };
    // No timeout. The thread is asleep until the kernel has an event or `stop`
    // writes to the pipe, which is what makes an idle watcher free.
    const int ready = ::poll(fds, 2, -1);
    if(ready < 0) {
      if(errno == EINTR) continue;
      return;
    }
    if(stopping_.load(std::memory_order_acquire)) return;
    if((fds[1].revents & POLLIN) != 0) return;
    if((fds[0].revents & POLLIN) == 0) continue;

    bool anything = false;
    while(true) {
      const ssize_t read = ::read(inotify, buffer.data(), buffer.size());
      if(read < 0) {
        if(errno == EINTR) continue;
        break;  // EAGAIN: the queue is drained.
      }
      if(read == 0) break;
      std::lock_guard<std::mutex> lock(mutex_);
      for(ssize_t at = 0; at + static_cast<ssize_t>(sizeof(inotify_event)) <= read;) {
        const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + at);
        at += static_cast<ssize_t>(sizeof(inotify_event)) + event->len;

        if((event->mask & IN_Q_OVERFLOW) != 0) {
          // Events were dropped by the kernel, so the list of paths is no longer
          // the list of what changed.
          requestRescan();
          anything = true;
          continue;
        }
        const auto directory = directories_.find(event->wd);
        if(directory == directories_.end()) continue;

        if((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
          // A directory this was watching is gone. Whatever was inside it went
          // with it, and inotify will not say what, so the caller re-reads.
          ::inotify_rm_watch(inotify, event->wd);
          directories_.erase(directory);
          requestRescan();
          anything = true;
          continue;
        }
        if(event->len == 0) continue;

        const std::filesystem::path path = directory->second / event->name;
        if((event->mask & IN_ISDIR) != 0) {
          // A new directory has to be watched before anything appears in it, and
          // a directory that arrived by rename may already be full -- so its
          // contents are a rescan rather than a list.
          if((event->mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
            const auto name = path.filename().string();
            if(std::find(ignored_.begin(), ignored_.end(), name) == ignored_.end()) {
              if(!addTree(path)) requestRescan();
              requestRescan();
              anything = true;
            }
          } else if((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0) {
            requestRescan();
            anything = true;
          }
          continue;
        }
        // The app's own staging files. A durable write puts one beside its
        // target for the length of a rename, and reporting it would have the
        // caller chasing a file that is about to stop existing under that name.
        if(isTemporaryWriteName(event->name)) continue;
        changed_.insert(path.string());
        anything = true;
      }
    }

    if(!anything) continue;
    WakeFn wake;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      wake = wake_;
    }
    // Called outside the lock: it runs arbitrary caller code, and holding the
    // mutex across it would let a wake that touches this watcher deadlock.
    if(wake) wake();
  }
}

std::vector<std::filesystem::path> DirectoryWatcher::takeChanges() {
  std::unordered_set<std::string> taken;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(changed_);
  }
  std::vector<std::filesystem::path> paths;
  paths.reserve(taken.size());
  for(auto& path : taken) paths.emplace_back(std::move(path));
  return paths;
}

bool DirectoryWatcher::takeRescanRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool requested = rescanRequested_;
  rescanRequested_ = false;
  return requested;
}

void DirectoryWatcher::closeDescriptors() {
  closeIfOpen(inotify_);
  closeIfOpen(stopRead_);
  closeIfOpen(stopWrite_);
  directories_.clear();
}

void DirectoryWatcher::stop() {
  if(!worker_.joinable()) {
    std::lock_guard<std::mutex> lock(mutex_);
    closeDescriptors();
    return;
  }
  stopping_.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopWrite_ >= 0) {
      const char byte = 1;
      // Best effort: a full pipe already holds a wake-up the thread has not
      // read yet, which is all this needs to achieve.
      (void)!::write(stopWrite_, &byte, 1);
    }
  }
  worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  closeDescriptors();
  changed_.clear();
  rescanRequested_ = false;
}

}
