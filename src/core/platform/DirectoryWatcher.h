#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace microcore::platform {

// Watches a directory tree and says which files in it changed.
//
// This exists so that "the file changed on disk" does not have to wait for the
// window to regain focus. A note edited in another program, a `git checkout`, a
// sync daemon delivering a change -- all of them happen while the app is
// sitting there, and until something noticed, the copy on screen was stale and
// the next autosave wrote it back over the top.
//
// **Nothing is polled.** One `inotify` descriptor, a watch per directory, and a
// thread blocked in `poll` until the kernel has something to say. Idle cost is
// zero: no timer, no stat, no wake-up. That is the whole reason for the
// complexity here -- a one-second `stat` of the open file would have been ten
// lines, and would have meant waking a sleeping process once a second forever
// to be told nothing had happened.
//
// The thread does not touch anything the caller owns. It collects paths into a
// set behind a mutex and calls `wake` -- one function, whose only job is to make
// the caller's event loop run a frame. The caller then drains the set with
// `takeChanges()` on its own thread. Coalescing is free and deliberate: an
// editor writing a file produces several events, and a caller that wants to
// re-index it once should be told about it once.
//
// `takeRescanRequest()` is the escape hatch, and it matters more than it looks.
// A watcher can lose track: the kernel's queue can overflow, a watched
// directory can be moved away, and a tree can be larger than the per-user watch
// limit. Every one of those means "some change happened that I cannot name", so
// it is reported as such rather than swallowed -- a watcher that quietly stops
// working is worse than no watcher, because the caller stops checking for
// itself.
class DirectoryWatcher {
public:
  using WakeFn = std::function<void()>;

  DirectoryWatcher() = default;
  ~DirectoryWatcher();
  DirectoryWatcher(const DirectoryWatcher&) = delete;
  DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

  // Called from the watcher's own thread when there is something to collect.
  // Set it before `watch`. It must be safe to call from any thread and should
  // do nothing but nudge the caller's loop awake.
  void setWake(WakeFn wake);

  // Watches `root` and every directory under it, except any whose name is in
  // `ignoredNames` -- which is how the app's own state directory stays out of
  // it, since the sqlite index, its WAL and every attachment live there and
  // rewriting the index must not look like a change to the library.
  //
  // Replaces whatever was being watched. False when the tree could not be
  // watched at all, in which case the caller is on its own and should keep
  // refreshing the way it did before.
  bool watch(std::filesystem::path root, std::vector<std::string> ignoredNames = {});
  void stop();
  bool active() const;

  // Absolute paths reported changed since the last call, deduplicated, and
  // cleared by the call. A path may name a file that no longer exists: a
  // deletion is a change.
  std::vector<std::filesystem::path> takeChanges();

  // Whether something happened that cannot be named as a list of paths, and the
  // caller should re-read everything. Cleared by the call.
  bool takeRescanRequest();

  // Directories currently watched. Diagnostics, and a test seam.
  std::size_t watchCount() const;

  // The most directories one tree may take before the watcher gives up naming
  // paths and asks for rescans instead. Well under the usual
  // `max_user_watches`, because a watcher is not entitled to spend the whole
  // per-user budget on one window.
  static constexpr std::size_t kMaxWatches = 8192;

private:
  void run();
  // Adds a watch on `dir` and, recursively, on everything under it. Returns
  // false once the budget is spent, which puts the watcher in rescan-only mode.
  bool addTree(const std::filesystem::path& dir);
  void closeDescriptors();
  void requestRescan();

  mutable std::mutex mutex_;
  std::filesystem::path root_;
  std::vector<std::string> ignored_;
  // Watch descriptor to the directory it names, so an event's `name` field can
  // be turned back into a path. inotify reports the directory, never the file.
  std::unordered_map<int, std::filesystem::path> directories_;
  std::unordered_set<std::string> changed_;
  bool rescanRequested_ = false;

  WakeFn wake_;
  int inotify_ = -1;
  // The read end is polled alongside inotify so `stop` can interrupt a thread
  // that is otherwise blocked indefinitely. A pipe rather than a timeout: a
  // timeout means waking up to check whether it is time to exit, forever.
  int stopRead_ = -1;
  int stopWrite_ = -1;
  std::atomic<bool> stopping_ {false};
  std::thread worker_;
};

}
