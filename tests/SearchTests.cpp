#include "TestSupport.h"

#include <sqlite3.h>

#include <chrono>

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"
#include "library/Organization.h"
#include "core/perf/PerformanceCounters.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>

namespace {

// Points the index at a scratch XDG_DATA_HOME so a test run never touches the
// real one, and restores whatever was there on the way out.
class ScopedXdgDataHome {
public:
  explicit ScopedXdgDataHome(const std::filesystem::path& path) {
    if(const char* current = std::getenv("XDG_DATA_HOME")) {
      previous_ = current;
      hadPrevious_ = true;
    }
    setenv("XDG_DATA_HOME", path.c_str(), 1);
  }

  ~ScopedXdgDataHome() {
    if(hadPrevious_) setenv("XDG_DATA_HOME", previous_.c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
  }

private:
  std::string previous_;
  bool hadPrevious_ = false;
};

}


MICRONOTES_TEST(library_index_searches_file_backed_rows) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-file-backed-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "today-note";
  metadata.title = "Today";
  library.createNote(metadata, "daily body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  const auto results = index.search("Today");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "today-note");
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_rebuilds_sqlite_cache_from_files) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-search";
  metadata.title = "SQLite Fast Path";
  library.createNote(metadata, "needle body\nmiddle\nsecond needle line");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "note-search");
  MICRONOTES_REQUIRE(results[0].matchLine == "needle body");
  MICRONOTES_REQUIRE(results[0].snippets.size() == 2);
  // Where in each line the query landed, so the sidebar can mark the match
  // rather than the whole line -- and trim a long line around it rather than
  // ellipsizing the match itself away.
  MICRONOTES_REQUIRE(results[0].snippets[0].matchStart == 0);
  MICRONOTES_REQUIRE(results[0].snippets[0].matchLength == 6);
  MICRONOTES_REQUIRE(results[0].snippets[1].matchLine == "second needle line");
  MICRONOTES_REQUIRE(results[0].snippets[1].matchStart == 7);
  MICRONOTES_REQUIRE(results[0].snippets[1].matchLength == 6);
  MICRONOTES_REQUIRE(results[0].matchStart == results[0].snippets[0].matchStart);
  const auto partial = index.search("eedle bo");
  MICRONOTES_REQUIRE(partial.size() == 1);
  MICRONOTES_REQUIRE(partial[0].id == "note-search");
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Content).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Title).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Content).size() == 1);
  MICRONOTES_REQUIRE(std::filesystem::exists(root / ".micronotes" / "index.sqlite"));
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_preserves_search_after_note_move) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-move-test";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-move";
  metadata.title = "Movable";
  const auto path = library.createNote(metadata, "moving needle body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  const auto moved = library.moveNote(path, "archive");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "note-move");
  MICRONOTES_REQUIRE(results[0].path == moved);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_note_from_sqlite) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-delete-note-test";
  std::filesystem::remove_all(root);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "delete-note";
  metadata.title = "Delete Note";
  const auto path = library.createNote(metadata, "deleted searchable body");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").size() == 1);
  library.deleteNote(path);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").empty());
  MICRONOTES_REQUIRE(index.size() == 0);

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_removes_trashed_folder_notes_from_sqlite) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-delete-folder-test";
  std::filesystem::remove_all(root);

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata first;
  first.id = "delete-folder-1";
  first.title = "Folder One";
  auto firstPath = library.createNote(first, "folder-one searchable body");
  library.moveNote(firstPath, "work");
  micronotes::library::NoteMetadata second;
  second.id = "delete-folder-2";
  second.title = "Folder Two";
  auto secondPath = library.createNote(second, "folder-two searchable body");
  library.moveNote(secondPath, "work/nested");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").size() == 2);
  library.deleteFolder("work");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("searchable").empty());
  MICRONOTES_REQUIRE(index.size() == 0);

  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(organization_lists_folders_tags_and_notes) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-org-test";
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
}

// The note list is a `SELECT` over the index. It used to be a second recursive
// walk of the library plus an open and a front-matter parse of every note in it,
// microseconds after the refresh had read the same files for the same fields.
// Both walks had counters, which is what makes the fix checkable rather than
// plausible.
MICRONOTES_TEST(organization_reads_the_note_list_out_of_the_index) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-org-from-index";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

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
  std::filesystem::remove_all(root);
}

// A library whose index will not open still lists its notes. The fallback is a
// walk and a front-matter parse per note -- what every list used to cost -- and
// it is the reason the index is not load-bearing for reading a library.
MICRONOTES_TEST(organization_falls_back_to_the_tree_without_an_index) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-org-no-index";
  std::filesystem::remove_all(root);

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
  std::filesystem::remove_all(root);
}

// --- refresh cost ------------------------------------------------------------
//
// refreshChangedFiles runs on every window focus. It used to open a connection,
// compile five statements, and take BEGIN IMMEDIATE -- forcing a write-lock and
// a WAL commit -- before it had established whether anything had changed at
// all. These tests pin the shape of the fix; a regression is invisible from the
// UI, which is exactly why it needs a test.

MICRONOTES_TEST(library_index_uses_one_connection_for_its_lifetime) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-conn";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "One";
  library.createNote(metadata, "body one");

  microcore::perf::resetCounters();
  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  index.refreshChangedFiles();
  index.refreshChangedFiles();
  (void)index.search("body");

  MICRONOTES_REQUIRE(
    microcore::perf::readCounter(microcore::perf::CounterId::SqliteConnectionOpens) == 1);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(library_index_refresh_writes_nothing_when_nothing_changed) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-noop";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  for(int i = 0; i < 5; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n" + std::to_string(i);
    metadata.title = "Note " + std::to_string(i);
    library.createNote(metadata, "body " + std::to_string(i));
  }

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());  // cold: indexes everything
  MICRONOTES_REQUIRE(index.size() == 5);

  // A second refresh with an untouched tree must not begin a transaction, and
  // must not re-read a single note body.
  const auto execBefore = microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls);
  const auto rereadBefore = microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls) == execBefore);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == rereadBefore);
  MICRONOTES_REQUIRE(index.size() == 5);

  // Changing one note must still be picked up, and must re-read only that one.
  library.createNote([]{
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n99";
    metadata.title = "Added";
    return metadata;
  }(), "a new body");
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == rereadBefore + 1);
  MICRONOTES_REQUIRE(index.size() == 6);
  std::filesystem::remove_all(root);
}

// The walk used to descend into the state directory -- the sqlite index, its
// WAL, and every attachment -- and then discard the results by comparing path
// prefixes, rebuilding the prefix string for every entry in the tree.
MICRONOTES_TEST(library_index_scan_does_not_descend_into_the_state_directory) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-statedir";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "One";
  library.createNote(metadata, "body");

  // Plant files inside the state directory that the walk must never visit.
  const auto attachments = root / ".micronotes" / "attachments" / "n1";
  std::filesystem::create_directories(attachments);
  for(int i = 0; i < 25; ++i) {
    std::ofstream out(attachments / ("file" + std::to_string(i) + ".md"));
    out << "not a note";
  }

  microcore::perf::resetCounters();
  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  // One real note indexed, and the 25 planted .md files never even visited.
  MICRONOTES_REQUIRE(index.size() == 1);
  const auto visited = microcore::perf::readCounter(microcore::perf::CounterId::LibraryDirectoryEntriesVisited);
  MICRONOTES_REQUIRE(visited < 25);
  std::filesystem::remove_all(root);
}


namespace {

// A scratch library with one note per (title, body) pair, indexed and ready to
// be asked about. Named after the test so two of them cannot collide.
struct BacklinkFixture {
  explicit BacklinkFixture(std::string name)
    : root(std::filesystem::temp_directory_path() / ("micronotes-backlinks-" + name)),
      library(root) {
    std::filesystem::remove_all(root);
  }

  ~BacklinkFixture() {
    std::filesystem::remove_all(root);
  }

  void note(const std::string& title, std::string_view body) const {
    micronotes::library::NoteMetadata metadata;
    metadata.id = title;
    metadata.title = title;
    library.createNote(metadata, body);
  }

  std::filesystem::path root;
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
  MICRONOTES_REQUIRE(index.open(fixture.root));
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
  MICRONOTES_REQUIRE(index.open(fixture.root));
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
  MICRONOTES_REQUIRE(index.open(fixture.root));
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
  MICRONOTES_REQUIRE(index.open(fixture.root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").size() == 1);

  for(const auto& entry : std::filesystem::directory_iterator(fixture.root)) {
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
  MICRONOTES_REQUIRE(index.open(fixture.root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.backlinks("Target", "Target").empty());
}

// The match range is a byte offset into the line as written, not into a
// lowercased copy of it, so a query in the other case still points at the
// right bytes.
MICRONOTES_TEST(library_index_locates_a_match_regardless_of_case) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-match-case";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "cased";
  metadata.title = "Cased";
  library.createNote(metadata, "The Needle is capitalised here\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].snippets.size() == 1);
  const auto& snippet = results[0].snippets.front();
  MICRONOTES_REQUIRE(snippet.matchStart == 4);
  MICRONOTES_REQUIRE(snippet.matchLine.substr(snippet.matchStart, snippet.matchLength) == "Needle");
  std::filesystem::remove_all(root);
}

// A query matching hundreds of lines of one note used to build a snippet per
// line, three strings each, and throw all but three away -- per note, on every
// keystroke of the query.
MICRONOTES_TEST(library_index_keeps_only_the_snippets_anything_will_draw) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-snippet-cap";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "many";
  metadata.title = "Many";
  std::string body;
  for(int i = 0; i < 500; ++i) body += "a needle on line " + std::to_string(i) + "\n";
  library.createNote(metadata, body);

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].snippets.size() == 3);
  // Still the first three lines, in order, rather than an arbitrary three.
  MICRONOTES_REQUIRE(results[0].snippets[0].matchLine == "a needle on line 0");
  MICRONOTES_REQUIRE(results[0].snippets[2].matchLine == "a needle on line 2");
  std::filesystem::remove_all(root);
}

// The fts row for a note is filed under the note row's own rowid, which is what
// lets a re-index delete it without scanning the whole index. Get that wrong
// and the failure is not an error: it is an old copy of the note left behind in
// the index, so a word the writer deleted keeps returning the note for as long
// as the library exists.
MICRONOTES_TEST(library_index_replaces_a_note_rather_than_adding_a_second_copy) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-reindex";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "reindexed";
  metadata.title = "Reindexed";
  const auto path = library.createNote(metadata, "the original zarquon body");
  // A second note, so the index has more than one row and a rowid mix-up has
  // somewhere to go.
  micronotes::library::NoteMetadata other;
  other.id = "other";
  other.title = "Other";
  library.createNote(other, "an unrelated note");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("zarquon").size() == 1);

  // Rewrite it. The mtime has to move or the refresh is right to skip the file.
  library.saveNote(path, metadata, "the replacement blorple body");
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() +
                                           std::chrono::seconds(2));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());
  MICRONOTES_REQUIRE(index.search("unrelated").size() == 1);

  // And a removal takes its fts row with it.
  std::filesystem::remove(path);
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").empty());
  MICRONOTES_REQUIRE(index.search("unrelated").size() == 1);
  std::filesystem::remove_all(root);
}

// An index built by an earlier run is reopened, not rebuilt, so whatever
// `migrate` decided about it has to leave the standing rows searchable.
MICRONOTES_TEST(library_index_reopens_an_existing_index_and_still_finds_its_notes) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-reopen";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "kept";
  metadata.title = "Kept";
  library.createNote(metadata, "a note with the word plugh in it");
  {
    micronotes::library::LibraryIndex index;
    MICRONOTES_REQUIRE(index.open(root));
    MICRONOTES_REQUIRE(index.refreshChangedFiles());
    MICRONOTES_REQUIRE(index.search("plugh").size() == 1);
  }
  micronotes::library::LibraryIndex reopened;
  MICRONOTES_REQUIRE(reopened.open(root));
  MICRONOTES_REQUIRE(reopened.search("plugh").size() == 1);
  // And a refresh over an unchanged tree neither loses nor duplicates it.
  MICRONOTES_REQUIRE(reopened.refreshChangedFiles());
  MICRONOTES_REQUIRE(reopened.search("plugh").size() == 1);
  std::filesystem::remove_all(root);
}

// A snippet is a three-line window, and which three lines it is is the part a
// streaming walk over the body can get wrong: the line above has to be the one
// the previous turn of the loop saw, and the line below is not known until the
// turn after. The first and last lines of a note have no neighbour on one side,
// which is where an off-by-one shows up as somebody else's text.
MICRONOTES_TEST(library_index_snippets_carry_the_lines_around_the_match) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-snippet-window";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "window";
  metadata.title = "Window";
  library.createNote(metadata, "needle at the top\nsecond\nthird\nneedle in the middle\nfifth\nneedle at the end");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  const auto& snippets = results[0].snippets;
  MICRONOTES_REQUIRE(snippets.size() == 3);
  // Nothing above the first line of the note.
  MICRONOTES_REQUIRE(snippets[0].beforeLine.empty());
  MICRONOTES_REQUIRE(snippets[0].matchLine == "needle at the top");
  MICRONOTES_REQUIRE(snippets[0].afterLine == "second");
  MICRONOTES_REQUIRE(snippets[1].beforeLine == "third");
  MICRONOTES_REQUIRE(snippets[1].matchLine == "needle in the middle");
  MICRONOTES_REQUIRE(snippets[1].afterLine == "fifth");
  // Nothing below the last, and no trailing newline to invent one.
  MICRONOTES_REQUIRE(snippets[2].beforeLine == "fifth");
  MICRONOTES_REQUIRE(snippets[2].matchLine == "needle at the end");
  MICRONOTES_REQUIRE(snippets[2].afterLine.empty());
  // The result's own fields mirror the first snippet.
  MICRONOTES_REQUIRE(results[0].beforeLine == snippets[0].beforeLine);
  MICRONOTES_REQUIRE(results[0].afterLine == snippets[0].afterLine);
  std::filesystem::remove_all(root);
}

// `%` and `_` are SQL LIKE's own wildcards. A query carrying one used to match
// notes that do not contain the query at all, and each of those rows drew a
// title with nothing under it -- the snippet under a result is found by a
// literal search of the body, and there was nothing literal there to find.
MICRONOTES_TEST(library_index_treats_sql_wildcards_as_ordinary_characters) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-wildcards";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "w";
  metadata.title = "Wildcards";
  library.createNote(metadata, "growth was 50 percent\nand ab_cd here\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());

  // "a%t" would have matched every note with an `a` somewhere before a `t`.
  MICRONOTES_REQUIRE(index.search("a%t").empty());
  MICRONOTES_REQUIRE(index.search("50%").empty());
  // A literal underscore still finds the text that literally has one, and an
  // underscore must not stand in for the character beside it.
  MICRONOTES_REQUIRE(index.search("ab_cd").size() == 1);
  MICRONOTES_REQUIRE(index.search("ab_d").empty());
  // A trailing backslash is escaped too, or it would escape the pattern's own
  // closing wildcard and match nothing at all by accident.
  MICRONOTES_REQUIRE(index.search("percent\\").empty());
  MICRONOTES_REQUIRE(index.search("percent").size() == 1);
  std::filesystem::remove_all(root);
}

// The refresh a save takes: one named file, no walk of the tree and no read of
// every row in the table. `refreshChangedFiles` exists to *discover* what
// changed and pays for the discovery; a save already knows.
MICRONOTES_TEST(library_index_refreshes_one_named_file_without_walking_the_tree) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-one-file";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  std::vector<std::filesystem::path> paths;
  for(int i = 0; i < 20; ++i) {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "n" + std::to_string(i);
    metadata.title = "Note " + std::to_string(i);
    paths.push_back(library.createNote(metadata, "body " + std::to_string(i)));
  }

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.size() == 20);

  microcore::perf::resetCounters();
  micronotes::library::NoteMetadata changed;
  changed.id = "n7";
  changed.title = "Note 7";
  MICRONOTES_REQUIRE(library.saveNote(paths[7], changed, "rewritten needle\n"));
  const auto refresh = index.refreshFile(paths[7]);
  MICRONOTES_REQUIRE(refresh.ok);
  // The five fields the note list is built from did not move, so the sidebar,
  // the folder counts and the tag list did not have to be rebuilt.
  MICRONOTES_REQUIRE(!refresh.listFieldsChanged);
  // One file read, and the tree never walked.
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 1);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryNoteFilesCalls) == 0);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexRefreshCalls) == 0);
  // And the body reached the search index.
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);
  MICRONOTES_REQUIRE(index.size() == 20);

  // A title change is a note-list change, and says so.
  changed.title = "Renamed 7";
  MICRONOTES_REQUIRE(library.saveNote(paths[7], changed, "rewritten needle\n"));
  MICRONOTES_REQUIRE(index.refreshFile(paths[7]).listFieldsChanged);

  // An unchanged file is a stat and nothing else: no read, no transaction.
  microcore::perf::resetCounters();
  const auto execBefore = microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls);
  MICRONOTES_REQUIRE(index.refreshFile(paths[7]).ok);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 0);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::SqliteExecCalls) == execBefore);

  // A file that is gone takes its rows with it, and that is a list change.
  std::filesystem::remove(paths[3]);
  const auto removed = index.refreshFile(paths[3]);
  MICRONOTES_REQUIRE(removed.ok);
  MICRONOTES_REQUIRE(removed.listFieldsChanged);
  MICRONOTES_REQUIRE(index.size() == 19);
  MICRONOTES_REQUIRE(index.search("body 3").empty());

  std::filesystem::remove_all(root);
}

// The form a save takes: the caller has just written the file, so it hands over
// the front matter and body instead of making the index read them back.
MICRONOTES_TEST(library_index_indexes_a_written_file_without_reading_it_back) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-written";
  std::filesystem::remove_all(root);
  ScopedXdgDataHome xdg(root / "xdg");

  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "n1";
  metadata.title = "Written";
  metadata.tags = {"alpha"};
  const auto path = library.createNote(metadata, "first body\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());

  microcore::perf::resetCounters();
  MICRONOTES_REQUIRE(library.saveNote(path, metadata, "second body with a needle\n"));
  const auto refresh = index.refreshWrittenFile(path, metadata, "second body with a needle\n");
  MICRONOTES_REQUIRE(refresh.ok);
  MICRONOTES_REQUIRE(!refresh.listFieldsChanged);
  // Nothing was read off the disk.
  MICRONOTES_REQUIRE(microcore::perf::readCounter(microcore::perf::CounterId::LibraryIndexFilesReread) == 0);
  // And the rows say the same thing they would have if it had been.
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);
  MICRONOTES_REQUIRE(index.search("first body").empty());
  const auto notes = index.notes();
  MICRONOTES_REQUIRE(notes.size() == 1);
  MICRONOTES_REQUIRE(notes.front().title == "Written");
  MICRONOTES_REQUIRE(notes.front().tags.size() == 1);
  MICRONOTES_REQUIRE(notes.front().tags.front() == "alpha");

  // The handed-over form and the read form have to agree, or a save and a
  // watcher wake-up would index the same note differently.
  MICRONOTES_REQUIRE(!index.refreshFile(path).listFieldsChanged);
  MICRONOTES_REQUIRE(index.search("needle").size() == 1);

  std::filesystem::remove_all(root);
}

// The index used to carry a second copy of every note: `notes.body` and
// `notes_fts.body` both held the whole text, so a 10.3 MB library indexed to
// 27.2 MB. The full-text table stores the terms and not the text now -- the
// only thing ever asked of it is which rowids match, and the snippets are built
// from `notes.body` through the join.
//
// The shape depends on the SQLite the build is linked against, because a
// contentless fts5 table can only have a row deleted from 3.43 onwards and
// deleting a row is what every save does. Both shapes have to answer every
// question identically, which is what makes the older one a fallback rather
// than a second mode -- so the assertions below are about *search*, and the
// shape is only read to say which one they were checked against.
MICRONOTES_TEST(library_index_stores_the_terms_rather_than_the_text) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-index-contentless";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "terms";
  metadata.title = "Terms";
  const auto path = library.createNote(metadata, "a note about zarquon and nothing else");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.ftsStoresBodies() == (sqlite3_libversion_number() < 3043000));

  // Matching, in all three scopes.
  MICRONOTES_REQUIRE(index.search("zarquon").size() == 1);
  MICRONOTES_REQUIRE(index.search("Terms", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon", micronotes::library::SearchScope::Content).size() == 1);
  // And the snippet, which is the thing that would go missing if it had been
  // coming out of the full-text table rather than out of `notes`.
  MICRONOTES_REQUIRE(index.search("zarquon").front().matchLine.find("zarquon") !=
                     std::string::npos);

  // A rewrite has to retire the old terms. This is the operation a contentless
  // table cannot perform before 3.43, and getting it wrong leaves a note
  // matching words it no longer contains.
  library.saveNote(path, metadata, "a note about blorple and nothing else");
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() +
                                           std::chrono::seconds(2));
  MICRONOTES_REQUIRE(index.refreshChangedFiles());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());

  // And a full rebuild, which empties the table rather than deleting rows.
  MICRONOTES_REQUIRE(index.rebuild());
  MICRONOTES_REQUIRE(index.search("blorple").size() == 1);
  MICRONOTES_REQUIRE(index.search("zarquon").empty());
  std::filesystem::remove_all(root);
}
