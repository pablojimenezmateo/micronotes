#include "TestSupport.h"
#include "ScopedXdgDataHome.h"
#include "TempDir.h"

#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "library/Metadata.h"
#include "library/Organization.h"
#include "core/perf/PerformanceCounters.h"

#include <filesystem>
#include <string>

// The organization service: the folders, the tags and the note list the sidebar
// reads, out of the index where there is one and out of the tree where there is
// not.
//
// Split out of `SearchTests.cpp`, where it sat because it takes a
// `LibraryIndex` in its constructor.

MICRONOTES_TEST(organization_lists_folders_tags_and_notes) {
  const micronotes::tests::TempDir rootDir("micronotes-org-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata work;
  work.id = "work-note";
  work.title = "Alpha";
  work.tags = {"work", "fast"};
  const auto path = library.createNote(work, "body");
  library.moveNote(path, "work");

  micronotes::library::NoteMetadata personal;
  personal.id = "personal-note";
  personal.title = "Beta";
  personal.tags = {"home"};
  library.createNote(personal, "body");
  library.createFolder("ideas");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  micronotes::library::OrganizationService org(library, index);
  MICRONOTES_REQUIRE(org.folders().size() == 3);
  MICRONOTES_REQUIRE(org.tags().size() == 3);
  MICRONOTES_REQUIRE(org.notesInFolder("ideas").empty());
  MICRONOTES_REQUIRE(org.notesInFolder("work").size() == 1);
  MICRONOTES_REQUIRE(org.notesWithTag("fast").size() == 1);

  // The list came out of the index, not off the disk: the note's tags and icon
  // are columns now, so nothing had to be re-opened and re-parsed to find them.
  const auto* alpha = org.noteById("work-note");
  MICRONOTES_REQUIRE(alpha != nullptr);
  MICRONOTES_REQUIRE(alpha->title == "Alpha");
  MICRONOTES_REQUIRE(alpha->tags.size() == 2);
  MICRONOTES_REQUIRE(alpha->folder == "work");
  MICRONOTES_REQUIRE(alpha->path == root / "work" / alpha->path.filename());
}

// The note list is a `SELECT` over the index. It used to be a second recursive
// walk of the library plus an open and a front-matter parse of every note in it,
// microseconds after the refresh had read the same files for the same fields.
// Both walks had counters, which is what makes the fix checkable rather than
// plausible.
MICRONOTES_TEST(organization_reads_the_note_list_out_of_the_index) {
  const micronotes::tests::TempDir rootDir("micronotes-org-from-index");
  const auto& root = rootDir.path();
  micronotes::tests::ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  for(int i = 0; i < 6; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n" + std::to_string(i);
    metadata.title = "Note " + std::to_string(i);
    metadata.tags = {"alpha", "beta"};
    metadata.icon = "\xf0\x9f\x93\x9d";
    library.createNote(metadata, "body");
  }
  library.createFolder("empty-folder");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  using microcore::perf::CounterId;
  microcore::perf::resetCounters();
  micronotes::library::OrganizationService org(library, index);
  const auto& notes = org.notes();
  MICRONOTES_REQUIRE(notes.size() == 6);
  // One statement, six rows, and not one walk of the tree.
  MICRONOTES_REQUIRE(microcore::perf::readCounter(CounterId::LibraryNoteRowsSelected) == 6);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(CounterId::LibraryNoteFilesCalls) == 0);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(CounterId::LibraryDirectoryEntriesVisited) == 0);

  // Tags and the icon survive the round trip through the row, and the empty
  // folder is still in the tree -- it comes from the refresh's own walk, which
  // is the one walk the startup now makes.
  MICRONOTES_REQUIRE(notes.front().tags.size() == 2);
  MICRONOTES_REQUIRE(notes.front().icon == "\xf0\x9f\x93\x9d");
  MICRONOTES_REQUIRE(org.tags().size() == 2);
  bool sawEmpty = false;
  for(const auto& folder : org.folders()) {
    if(folder.path == "empty-folder") sawEmpty = true;
  }
  MICRONOTES_REQUIRE(sawEmpty);
}

// A library whose index will not open still lists its notes. The fallback is a
// walk and a front-matter parse per note -- what every list used to cost -- and
// it is the reason the index is not load-bearing for reading a library.
MICRONOTES_TEST(organization_falls_back_to_the_tree_without_an_index) {
  const micronotes::tests::TempDir rootDir("micronotes-org-no-index");
  const auto& root = rootDir.path();

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "only";
  metadata.title = "Only";
  metadata.tags = {"solo"};
  library.createNote(metadata, "body");

  const micronotes::library::LibraryIndex unopened;
  micronotes::library::OrganizationService org(library, unopened);
  MICRONOTES_REQUIRE(org.notes().size() == 1);
  MICRONOTES_REQUIRE(org.notes().front().title == "Only");
  MICRONOTES_REQUIRE(org.tags().size() == 1);
}
