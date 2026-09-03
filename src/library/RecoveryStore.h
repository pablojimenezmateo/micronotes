#pragma once

#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <cstdint>

namespace micronotes::library {

// The crash-recovery copy of the buffer being edited: written durably, and not
// on the thread that is drawing the window.
//
// Recovery has to be posted on every keystroke -- that is the whole point of
// it, a crash between two characters must not lose the second one. But a
// durable write is two `fsync` barriers, and on ext4 here that measured 1.1 ms
// median and up to 4 ms per character, against a 2 ms frame budget. It is the
// same 0.95 ms for a 1 KB note as for a 200 KB one, which is what says the cost
// is the barriers and not the bytes; and with the disk busy elsewhere it went
// to 8.7 ms median and 35 ms worst. It was by far the largest thing in a
// keystroke -- twenty-five times the whole layout update -- and it was
// invisible to every instrument in `docs/performance.md`, because none of them
// go through the app's key handling.
//
// The write is what has to be durable. Waiting for it never was. So a post is a
// copy into a one-slot-per-note mailbox plus a notify, and one writer thread
// drains it. Latest-wins per note: typing that arrives faster than the disk can
// retire it collapses to a single write of the newest text, which is the only
// version anyone would ever want back. A 200-character burst posts 200 times,
// costs the UI thread 7.8 us a character, and reaches the disk as two writes.
//
// What that gives up: the copy on disk trails the buffer by whatever write is in
// flight -- about a millisecond -- rather than being current at every character.
// A crash inside that window loses the characters typed during it. Against
// stalling the window for a millisecond per keystroke, and against the 1.2 s
// debounce the *real* save already runs on, that is the right trade.
//
// A clear is posted through the same mailbox, as a tombstone, rather than
// performed on the spot. Ordering is the reason: a direct removal could be
// overtaken by a save already in flight and resurrect the recovery file that a
// successful note-save had just retired, leaving the app offering to recover a
// draft it had already committed.
//
// `save` and `clear` must be called from one thread (the UI's). Everything else
// is safe from any.
class RecoveryStore {
public:
  RecoveryStore() = default;
  ~RecoveryStore();
  RecoveryStore(const RecoveryStore&) = delete;
  RecoveryStore& operator=(const RecoveryStore&) = delete;

  // Where the recovery directory lives. Waits for the queue to settle first, so
  // no write outlives the library it was addressed to.
  void setRoot(std::filesystem::path root);

  // Queues `body` as the recovery copy of `noteId`.
  //
  // Returns false when a write posted *earlier* has since failed. Reporting a
  // failure on a later call rather than on the one that caused it is the price
  // of not waiting for the disk, and it is the right trade for what this is:
  // the user needs to know recovery is not working, not which keystroke found
  // out. The flag is cleared by the read.
  bool save(std::string_view noteId, std::string_view body);
  // Queues the removal of `noteId`'s recovery copy. Same failure reporting.
  bool clear(std::string_view noteId);
  // The recovery copy of `noteId`, once everything queued for it has landed.
  std::optional<std::string> read(std::string_view noteId) const;
  // Blocks until the queue is empty and no write is in flight.
  void flush() const;

  // Diagnostics: posts that never reached the disk because a newer one replaced
  // them while the writer was busy.
  std::uint64_t coalesced() const;
  // Durable writes actually performed.
  std::uint64_t writes() const;

private:
  struct Pending {
    std::string body;
    bool remove = false;
    // How many posts landed in this slot before it was written. One means the
    // writer kept up; more means the mailbox coalesced that many keystrokes
    // into the single write the disk actually saw.
    std::uint64_t posts = 0;
  };

  void run();
  // Started on the first post rather than in the constructor: an `AppState` with
  // no library open, or one in a test, should not own a thread waiting for work
  // that never comes.
  void ensureWorker();
  std::filesystem::path pathFor(std::string_view noteId) const;
  // The body of `save`/`clear` they share, with `staging_` already filled.
  bool post(std::string_view noteId, bool remove);

  mutable std::mutex mutex_;
  mutable std::condition_variable posted_;
  mutable std::condition_variable drained_;
  std::filesystem::path root_;
  // Heterogeneous lookup, so a post finds an existing slot without building a
  // `std::string` key for it.
  std::map<std::string, Pending, std::less<>> queue_;
  bool inFlight_ = false;
  bool stop_ = false;
  bool failed_ = false;
  std::uint64_t coalesced_ = 0;
  std::uint64_t writes_ = 0;
  // The buffer a post copies into before taking the lock, swapped with the
  // mailbox slot rather than assigned into it. It comes back holding the text
  // the slot used to have, so it keeps that capacity and a steady stream of
  // keystrokes allocates nothing at all. UI thread only.
  std::string staging_;
  std::thread worker_;
};

}
