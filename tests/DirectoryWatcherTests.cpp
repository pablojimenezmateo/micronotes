#include "CoreAliases.h"
#include "TestSupport.h"

#include "core/platform/DirectoryWatcher.h"
#include "core/platform/DurableFile.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

using microcore::platform::DirectoryWatcher;

std::filesystem::path freshDir(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

// Waits for the watcher to have something, or gives up. The kernel delivers
// inotify events promptly but not synchronously, so a test that reads
// immediately is a test that fails on a loaded machine for no reason. The wait
// is a bound on the failure, not the expected duration -- the callback below
// normally fires within a millisecond.
bool waitForChanges(DirectoryWatcher& watcher, std::vector<std::filesystem::path>& out) {
  for(int attempt = 0; attempt < 400; ++attempt) {
    out = watcher.takeChanges();
    if(!out.empty()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

bool contains(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
  for(const auto& candidate : paths) {
    if(candidate == path) return true;
  }
  return false;
}

}

MICRONOTES_TEST(directory_watcher_reports_a_file_written_underneath_it) {
  const auto dir = freshDir("micronotes-watch-write");
  std::atomic<int> wakes {0};

  DirectoryWatcher watcher;
  watcher.setWake([&wakes] { wakes.fetch_add(1, std::memory_order_relaxed); });
  MICRONOTES_REQUIRE(watcher.watch(dir));
  MICRONOTES_REQUIRE(watcher.active());
  // Nothing has happened, so there is nothing to say.
  MICRONOTES_REQUIRE(watcher.takeChanges().empty());
  MICRONOTES_REQUIRE(!watcher.takeRescanRequest());

  const auto note = dir / "note.md";
  writeFile(note, "hello");
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));
  MICRONOTES_REQUIRE(wakes.load() > 0);
  // Drained by the read: the same change is not reported twice.
  MICRONOTES_REQUIRE(watcher.takeChanges().empty());

  std::filesystem::remove_all(dir);
}

// The case that matters most, and the one a naive mask misses: every careful
// writer -- this app included -- saves by writing a temp and renaming it into
// place, so an atomic save arrives as a move and never as a write.
MICRONOTES_TEST(directory_watcher_reports_an_atomic_save) {
  const auto dir = freshDir("micronotes-watch-atomic");
  DirectoryWatcher watcher;
  MICRONOTES_REQUIRE(watcher.watch(dir));

  const auto note = dir / "note.md";
  MICRONOTES_REQUIRE(microcore::platform::writeFileDurably(note, "durable"));
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));
  // The staging file the durable write left in the directory for the length of
  // the rename is not reported: it is about to stop existing under that name,
  // and chasing it would have the caller indexing a file that is not there.
  for(const auto& path : changed) {
    MICRONOTES_REQUIRE(!microcore::platform::isTemporaryWriteName(path.filename().native()));
  }

  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(directory_watcher_reports_a_deletion_and_a_nested_write) {
  const auto dir = freshDir("micronotes-watch-nested");
  const auto nested = dir / "folder" / "deeper";
  std::filesystem::create_directories(nested);
  const auto note = nested / "note.md";
  writeFile(note, "before");

  DirectoryWatcher watcher;
  MICRONOTES_REQUIRE(watcher.watch(dir));
  // Every directory under the root, not just the root.
  MICRONOTES_REQUIRE(watcher.watchCount() == 3);

  writeFile(note, "after");
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));

  // A deletion is a change: the caller has to hear about it to drop the rows.
  std::filesystem::remove(note);
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));

  std::filesystem::remove_all(dir);
}

// An ignored directory is how the app's own state stays out of it: the sqlite
// index, its write-ahead log and every attachment live in there, so watching it
// would turn the app's own index writes into "the library changed".
MICRONOTES_TEST(directory_watcher_never_looks_inside_an_ignored_directory) {
  const auto dir = freshDir("micronotes-watch-ignored");
  const auto state = dir / ".micronotes";
  std::filesystem::create_directories(state / "attachments");

  DirectoryWatcher watcher;
  MICRONOTES_REQUIRE(watcher.watch(dir, {".micronotes"}));
  MICRONOTES_REQUIRE(watcher.watchCount() == 1);

  writeFile(state / "index.sqlite-wal", "wal");
  writeFile(state / "attachments" / "thing.md", "attachment");
  // Give the kernel the same chance it gets in the passing tests above, so this
  // is "nothing arrived" rather than "nothing arrived yet".
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  MICRONOTES_REQUIRE(watcher.takeChanges().empty());
  MICRONOTES_REQUIRE(!watcher.takeRescanRequest());

  // A note beside it still reports, so the exclusion is the directory and not
  // the watcher having quietly died.
  const auto note = dir / "note.md";
  writeFile(note, "real");
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));

  std::filesystem::remove_all(dir);
}

// A directory that appears has to be watched before anything lands in it, and a
// directory that arrives by rename may already be full -- which no list of
// paths can describe, so it is reported as "re-read everything".
MICRONOTES_TEST(directory_watcher_asks_for_a_rescan_when_the_tree_changes_shape) {
  const auto dir = freshDir("micronotes-watch-shape");
  DirectoryWatcher watcher;
  MICRONOTES_REQUIRE(watcher.watch(dir));
  MICRONOTES_REQUIRE(watcher.watchCount() == 1);

  std::filesystem::create_directories(dir / "added");
  for(int attempt = 0; attempt < 400 && watcher.watchCount() < 2; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  MICRONOTES_REQUIRE(watcher.watchCount() == 2);
  MICRONOTES_REQUIRE(watcher.takeRescanRequest());
  // Cleared by the read.
  MICRONOTES_REQUIRE(!watcher.takeRescanRequest());

  // And the new directory really is watched.
  const auto note = dir / "added" / "note.md";
  writeFile(note, "inside the new folder");
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));

  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(directory_watcher_stops_and_restarts_cleanly) {
  const auto dir = freshDir("micronotes-watch-lifecycle");
  const auto other = freshDir("micronotes-watch-lifecycle-two");

  DirectoryWatcher watcher;
  MICRONOTES_REQUIRE(watcher.watch(dir));
  watcher.stop();
  MICRONOTES_REQUIRE(!watcher.active());
  // Stopping twice, and stopping something that never started, are both fine.
  watcher.stop();

  // `watch` on a live watcher replaces what it was watching, and the previous
  // tree stops reporting -- which is what opening a second library does.
  MICRONOTES_REQUIRE(watcher.watch(dir));
  MICRONOTES_REQUIRE(watcher.watch(other));
  MICRONOTES_REQUIRE(watcher.active());
  writeFile(dir / "ignored.md", "old root");
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  MICRONOTES_REQUIRE(watcher.takeChanges().empty());

  const auto note = other / "note.md";
  writeFile(note, "new root");
  std::vector<std::filesystem::path> changed;
  MICRONOTES_REQUIRE(waitForChanges(watcher, changed));
  MICRONOTES_REQUIRE(contains(changed, note));

  // A path that does not name a directory is refused rather than half-started.
  MICRONOTES_REQUIRE(!watcher.watch(dir / "nope"));
  MICRONOTES_REQUIRE(!watcher.active());

  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(other);
}
