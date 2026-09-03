#include "library/RecoveryStore.h"
#include "TestSupport.h"

#include <filesystem>
#include <string>

using micronotes::library::RecoveryStore;

namespace {

std::filesystem::path freshRoot(const char* name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

}

MICRONOTES_TEST(recovery_store_reads_back_what_was_posted) {
  const auto root = freshRoot("micronotes-recovery-store-roundtrip");
  {
    RecoveryStore store;
    store.setRoot(root);
    MICRONOTES_REQUIRE(store.save("note-1", "draft body"));
    const auto body = store.read("note-1");
    MICRONOTES_REQUIRE(body.has_value());
    MICRONOTES_REQUIRE(*body == "draft body");
  }
  std::filesystem::remove_all(root);
}

// The reason `clear` goes through the mailbox instead of removing the file. A
// direct removal races the saves already queued behind it: the last write wins,
// and if that write is an older save the app comes back offering to recover a
// draft it had already committed.
MICRONOTES_TEST(recovery_store_clear_cannot_be_overtaken_by_a_queued_save) {
  const auto root = freshRoot("micronotes-recovery-store-ordering");
  {
    RecoveryStore store;
    store.setRoot(root);
    // Posted back to back, with no flush between them, so they are in the queue
    // together and the order the writer applies them is what is under test.
    for(int i = 0; i < 64; ++i) store.save("note-1", "draft " + std::to_string(i));
    store.clear("note-1");
    MICRONOTES_REQUIRE(!store.read("note-1").has_value());
  }
  std::filesystem::remove_all(root);
}

// A save posted after a clear has to win, or a note edited again straight after
// being saved would have no recovery copy at all.
MICRONOTES_TEST(recovery_store_save_after_clear_wins) {
  const auto root = freshRoot("micronotes-recovery-store-resave");
  {
    RecoveryStore store;
    store.setRoot(root);
    store.save("note-1", "first");
    store.clear("note-1");
    store.save("note-1", "second");
    const auto body = store.read("note-1");
    MICRONOTES_REQUIRE(body.has_value());
    MICRONOTES_REQUIRE(*body == "second");
  }
  std::filesystem::remove_all(root);
}

// Two notes edited in the same session must not evict each other: the mailbox is
// one slot *per note*, not one slot.
MICRONOTES_TEST(recovery_store_keeps_a_slot_per_note) {
  const auto root = freshRoot("micronotes-recovery-store-two-notes");
  {
    RecoveryStore store;
    store.setRoot(root);
    store.save("note-a", "body a");
    store.save("note-b", "body b");
    const auto a = store.read("note-a");
    const auto b = store.read("note-b");
    MICRONOTES_REQUIRE(a.has_value() && *a == "body a");
    MICRONOTES_REQUIRE(b.has_value() && *b == "body b");
  }
  std::filesystem::remove_all(root);
}

// The point of the mailbox: a burst of keystrokes reaches the disk as far fewer
// durable writes than there were keystrokes, and the one that lands last is the
// newest text. Only the second half is asserted -- how much coalescing happens
// depends on how fast the disk retires a write, and a test that demanded a
// particular ratio would fail on a fast enough disk for the right reason.
MICRONOTES_TEST(recovery_store_coalesces_a_burst_to_the_newest_text) {
  const auto root = freshRoot("micronotes-recovery-store-coalesce");
  {
    RecoveryStore store;
    store.setRoot(root);
    std::string last;
    for(int i = 0; i < 200; ++i) {
      last = "draft " + std::to_string(i);
      store.save("note-1", last);
    }
    const auto body = store.read("note-1");
    MICRONOTES_REQUIRE(body.has_value());
    MICRONOTES_REQUIRE(*body == last);
    MICRONOTES_REQUIRE(store.writes() + store.coalesced() == 200);
  }
  std::filesystem::remove_all(root);
}

// Nothing is posted before a root is known, and a post against no root is
// refused rather than queued against the wrong directory.
MICRONOTES_TEST(recovery_store_refuses_a_post_with_no_root) {
  RecoveryStore store;
  MICRONOTES_REQUIRE(!store.save("note-1", "body"));
  MICRONOTES_REQUIRE(!store.read("note-1").has_value());
}

// Everything queued has to be on disk by the time the store goes away: the tail
// of the queue is the newest thing the user typed, and dropping it on shutdown
// is the loss the whole class exists to prevent.
MICRONOTES_TEST(recovery_store_drains_its_queue_before_it_is_destroyed) {
  const auto root = freshRoot("micronotes-recovery-store-shutdown");
  {
    RecoveryStore store;
    store.setRoot(root);
    for(int i = 0; i < 200; ++i) store.save("note-1", "draft " + std::to_string(i));
  }
  const auto path = root / ".micronotes" / "recovery" / "note-1.body";
  MICRONOTES_REQUIRE(std::filesystem::exists(path));
  MICRONOTES_REQUIRE(std::filesystem::file_size(path) > 0);
  std::filesystem::remove_all(root);
}
