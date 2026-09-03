#include "library/RecoveryStore.h"

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/DurableFile.h"
#include "core/platform/PathUtils.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace micronotes::library {

RecoveryStore::~RecoveryStore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  posted_.notify_all();
  // Joined rather than detached: the queue's tail is the newest text the user
  // typed, and dropping it on the way out is exactly the loss this class exists
  // to prevent. The worker finishes what it holds before it sees `stop_`.
  if(worker_.joinable()) worker_.join();
}

std::filesystem::path RecoveryStore::pathFor(std::string_view noteId) const {
  return root_ / ".micronotes" / "recovery" /
         (platform::sanitizeFileStem(std::string(noteId)) + ".body");
}

void RecoveryStore::setRoot(std::filesystem::path root) {
  flush();
  std::lock_guard<std::mutex> lock(mutex_);
  root_ = std::move(root);
}

void RecoveryStore::ensureWorker() {
  if(worker_.joinable()) return;
  worker_ = std::thread([this] { run(); });
}

bool RecoveryStore::post(std::string_view noteId, bool remove) {
  std::unique_lock<std::mutex> lock(mutex_);
  if(root_.empty() || noteId.empty()) return false;
  auto found = queue_.find(noteId);
  if(found == queue_.end()) found = queue_.emplace(std::string(noteId), Pending {}).first;
  Pending& slot = found->second;
  ++slot.posts;
  slot.remove = remove;
  slot.body.swap(staging_);
  ensureWorker();
  const bool failed = failed_;
  failed_ = false;
  lock.unlock();
  posted_.notify_one();
  perf::addCounter(perf::CounterId::RecoveryPosts);
  return !failed;
}

bool RecoveryStore::save(std::string_view noteId, std::string_view body) {
  // Copied before the lock is taken, into a buffer that already holds the last
  // post's text and so already has the capacity.
  staging_.assign(body);
  return post(noteId, false);
}

bool RecoveryStore::clear(std::string_view noteId) {
  staging_.clear();
  return post(noteId, true);
}

void RecoveryStore::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while(true) {
    drained_.notify_all();
    posted_.wait(lock, [this] { return stop_ || !queue_.empty(); });
    // Drain before stopping, never instead of it: `stop_` with work still
    // queued means the app is closing on text nobody has written down yet.
    if(queue_.empty()) {
      if(stop_) return;
      continue;
    }
    const auto entry = queue_.begin();
    const std::filesystem::path path = pathFor(entry->first);
    Pending work = std::move(entry->second);
    queue_.erase(entry);
    coalesced_ += work.posts - 1;
    inFlight_ = true;
    lock.unlock();
    const bool ok = work.remove ? platform::removeFileDurably(path)
                                : platform::writeFileDurably(path, work.body);
    lock.lock();
    inFlight_ = false;
    if(!ok) failed_ = true;
    ++writes_;
    perf::addCounter(perf::CounterId::RecoveryWrites);
  }
}

std::optional<std::string> RecoveryStore::read(std::string_view noteId) const {
  flush();
  std::filesystem::path path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(root_.empty() || noteId.empty()) return std::nullopt;
    path = pathFor(noteId);
  }
  std::ifstream file(path, std::ios::binary);
  if(!file) return std::nullopt;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void RecoveryStore::flush() const {
  std::unique_lock<std::mutex> lock(mutex_);
  drained_.wait(lock, [this] { return queue_.empty() && !inFlight_; });
}

std::uint64_t RecoveryStore::coalesced() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return coalesced_;
}

std::uint64_t RecoveryStore::writes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return writes_;
}

}
