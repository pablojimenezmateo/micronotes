#include "TestSupport.h"
#include "TempDir.h"

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"

#include <filesystem>
#include <string>
#include <string_view>

// The links table: who links to a note, written as the author wrote it.
//
// Split out of `SearchTests.cpp`. A backlink is not a search -- it is an exact
// lookup in a second table, populated by the indexer rather than by FTS -- and
// the two only shared a file because both go through `LibraryIndex`.

namespace {

// A scratch library with one note per (title, body) pair, indexed and ready to
// be asked about. Named after the test so two of them cannot collide.
struct BacklinkFixture {
  explicit BacklinkFixture(const std::string& name)
    : dir("micronotes-backlinks-" + name), library(dir.path()) {}

  void note(const std::string& title, std::string_view body) const {
    micronotes::library::NoteMetadata metadata;
    metadata.id = title;
    metadata.title = title;
    library.createNote(metadata, body);
  }

  const std::filesystem::path& root() const {
    return dir.path();
  }

  // `TempDir` rather than a hand-written pair of `remove_all`s, which is what
  // this was: the constructor's copy ran before the library existed and the
  // destructor's on the way out, so a failing assertion -- which throws --
  // left the tree behind. See `tests/TempDir.h`.
  micronotes::tests::TempDir dir;
  micronotes::library::Library library;
};

}

MICRONOTES_TEST(index_reports_who_links_to_a_note) {
  const BacklinkFixture fixture("who");
  fixture.note("Target", "# Target\n\nThe note being linked to.\n");
  fixture.note("One", "# One\n\nSee [[Target]] for the details.\n");
  fixture.note("Two", "# Two\n\nAlso [[Target|over there]].\n");
  fixture.note("Three", "# Three\n\nNothing to do with it.\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(fixture.root()));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  const auto links = index.backlinks("Target", "Target");
  MICRONOTES_REQUIRE(links.size() == 2);
  MICRONOTES_REQUIRE(links[0].title == "One");
  MICRONOTES_REQUIRE(links[1].title == "Two");
  // The line the link was written on comes back with it: without that, the
  // panel is a list of titles rather than a reason to click one.
  MICRONOTES_REQUIRE(links[0].line == "See [[Target]] for the details.");
  MICRONOTES_REQUIRE(links[1].line == "Also [[Target|over there]].");

  MICRONOTES_REQUIRE(index.backlinks("Three", "Three").empty());
  MICRONOTES_REQUIRE(index.backlinks("", "").empty());
}

// A link written in another case still counts, which is the same latitude
// resolveWikiLink gives it.
MICRONOTES_TEST(index_backlinks_ignore_case) {
  const BacklinkFixture fixture("case");
  fixture.note("Target", "# Target\n");
  fixture.note("One", "See [[target]].\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(fixture.root()));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").size() == 1);
}

// The whole reason the index stores the target as written rather than resolved:
// a rename changes what resolves without rewriting a single row.
MICRONOTES_TEST(index_backlinks_are_stored_as_written_not_resolved) {
  const BacklinkFixture fixture("rename");
  fixture.note("Old", "# Old\n");
  fixture.note("One", "See [[Old]].\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(fixture.root()));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Old", "Old").size() == 1);
  // Nothing names the new title yet, and the existing rows are untouched.
  MICRONOTES_REQUIRE(index.backlinks("New", "New").empty());
  MICRONOTES_REQUIRE(index.backlinks("Old", "Old").size() == 1);
}

// A note that goes away takes its outgoing links with it, rather than leaving
// a backlink pointing at nothing.
MICRONOTES_TEST(index_backlinks_drop_when_the_linking_note_does) {
  const BacklinkFixture fixture("removed");
  fixture.note("Target", "# Target\n");
  fixture.note("One", "See [[Target]].\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(fixture.root()));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").size() == 1);

  for(const auto& entry : std::filesystem::directory_iterator(fixture.root())) {
    if(entry.path().filename() == "One.md") std::filesystem::remove(entry.path());
  }
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").empty());
}

// A link inside a code span is text, not a link, and must not create a row.
MICRONOTES_TEST(index_backlinks_skip_a_link_inside_a_code_span) {
  const BacklinkFixture fixture("code");
  fixture.note("Target", "# Target\n");
  fixture.note("One", "Write it as `[[Target]]` to show the syntax.\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(fixture.root()));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").empty());
}
