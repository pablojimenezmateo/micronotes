#include "CoreAliases.h"
#include "TestSupport.h"

#include "core/platform/DurableFile.h"

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using microcore::platform::FileSignature;
using microcore::platform::isTemporaryWriteName;
using microcore::platform::removeFileDurably;
using microcore::platform::statFile;
using microcore::platform::writeFileDurably;

std::string readAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

mode_t modeOf(const std::filesystem::path& path) {
  struct stat status {};
  if(::stat(path.c_str(), &status) != 0) return 0;
  return static_cast<mode_t>(status.st_mode & 07777);
}

std::filesystem::path freshDir(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

}

MICRONOTES_TEST(durable_write_creates_parents_and_content) {
  const auto dir = freshDir("micronotes-durable-basic");
  const auto path = dir / "nested" / "deeper" / "note.md";
  MICRONOTES_REQUIRE(writeFileDurably(path, "hello"));
  MICRONOTES_REQUIRE(readAll(path) == "hello");
  // Nothing staged is left behind once the rename has landed.
  for(const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
    MICRONOTES_REQUIRE(!isTemporaryWriteName(entry.path().filename().native()));
  }
  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(durable_write_gathers_pieces_without_a_join) {
  const auto dir = freshDir("micronotes-durable-gather");
  const auto path = dir / "note.md";
  const std::string header = "---\nid: n1\n---\n\n";
  const std::string body = "the body\n";
  MICRONOTES_REQUIRE(writeFileDurably(path, {header, body}));
  MICRONOTES_REQUIRE(readAll(path) == header + body);
  // An empty piece is not a truncation.
  MICRONOTES_REQUIRE(writeFileDurably(path, {header, std::string_view {}}));
  MICRONOTES_REQUIRE(readAll(path) == header);
  std::filesystem::remove_all(dir);
}

// The bug this covers: a rename installs a fresh inode, so a note the user had
// deliberately made private came back world-readable on the first autosave.
MICRONOTES_TEST(durable_write_keeps_the_mode_of_the_file_it_replaces) {
  const auto dir = freshDir("micronotes-durable-mode");
  const auto path = dir / "private.md";
  MICRONOTES_REQUIRE(writeFileDurably(path, "first"));
  MICRONOTES_REQUIRE(::chmod(path.c_str(), 0600) == 0);
  MICRONOTES_REQUIRE(writeFileDurably(path, "second"));
  MICRONOTES_REQUIRE(modeOf(path) == 0600);
  MICRONOTES_REQUIRE(readAll(path) == "second");

  // An executable bit survives too, which is what says the whole mode is
  // carried rather than a hard-coded 0600 special case.
  MICRONOTES_REQUIRE(::chmod(path.c_str(), 0754) == 0);
  MICRONOTES_REQUIRE(writeFileDurably(path, "third"));
  MICRONOTES_REQUIRE(modeOf(path) == 0754);
  std::filesystem::remove_all(dir);
}

// The bug this covers: a note that is a symlink into another tree was replaced
// by a regular file, so the link died and every later edit went to a file the
// user was no longer pointing at.
MICRONOTES_TEST(durable_write_follows_a_symlink_to_its_target) {
  const auto dir = freshDir("micronotes-durable-symlink");
  const auto real = dir / "real.md";
  const auto link = dir / "link.md";
  MICRONOTES_REQUIRE(writeFileDurably(real, "original"));
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  MICRONOTES_REQUIRE(!error);

  MICRONOTES_REQUIRE(writeFileDurably(link, "through the link"));
  MICRONOTES_REQUIRE(std::filesystem::is_symlink(link));
  MICRONOTES_REQUIRE(readAll(real) == "through the link");

  // A dangling link is still a link: the write creates its target rather than
  // replacing the link node with a regular file.
  const auto dangling = dir / "dangling.md";
  std::filesystem::create_symlink(dir / "not-yet.md", dangling, error);
  MICRONOTES_REQUIRE(!error);
  MICRONOTES_REQUIRE(writeFileDurably(dangling, "created through a dangling link"));
  MICRONOTES_REQUIRE(std::filesystem::is_symlink(dangling));
  MICRONOTES_REQUIRE(readAll(dir / "not-yet.md") == "created through a dangling link");

  // A cycle must terminate rather than spin; whatever it writes, it returns.
  const auto loopA = dir / "a.md";
  const auto loopB = dir / "b.md";
  std::filesystem::create_symlink(loopB, loopA, error);
  std::filesystem::create_symlink(loopA, loopB, error);
  (void)writeFileDurably(loopA, "cycle");
  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(file_signature_separates_absent_from_unreadable) {
  const auto dir = freshDir("micronotes-durable-signature");
  const auto path = dir / "note.md";
  const FileSignature missing = statFile(path);
  MICRONOTES_REQUIRE(!missing.exists);
  MICRONOTES_REQUIRE(!missing.error);
  // An absent file never compares equal to anything, including another absence.
  MICRONOTES_REQUIRE(!missing.sameContentAs(missing));

  MICRONOTES_REQUIRE(writeFileDurably(path, "hello"));
  const FileSignature first = statFile(path);
  MICRONOTES_REQUIRE(first.exists);
  MICRONOTES_REQUIRE(first.size == 5);
  MICRONOTES_REQUIRE(first.sameContentAs(statFile(path)));

  // A different length is a change whatever the clock says.
  MICRONOTES_REQUIRE(writeFileDurably(path, "hello again"));
  MICRONOTES_REQUIRE(!first.sameContentAs(statFile(path)));
  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(durable_remove_is_idempotent) {
  const auto dir = freshDir("micronotes-durable-remove");
  const auto path = dir / "note.md";
  MICRONOTES_REQUIRE(writeFileDurably(path, "hello"));
  MICRONOTES_REQUIRE(removeFileDurably(path));
  MICRONOTES_REQUIRE(!std::filesystem::exists(path));
  // Already gone is the outcome the caller asked for, not a failure.
  MICRONOTES_REQUIRE(removeFileDurably(path));
  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(temporary_write_names_are_recognised) {
  MICRONOTES_REQUIRE(isTemporaryWriteName(".note.md.microcore-write.1234.0"));
  // An ordinary note that happens to be called something temporary-ish is not.
  MICRONOTES_REQUIRE(!isTemporaryWriteName("build.tmp.md"));
  MICRONOTES_REQUIRE(!isTemporaryWriteName("note.md"));
  MICRONOTES_REQUIRE(!isTemporaryWriteName(".hidden.md"));
}
