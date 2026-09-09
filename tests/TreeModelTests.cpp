#include "TestSupport.h"

#include "ui/TreeModel.h"

#include <string>
#include <vector>

namespace {

using micronotes::library::CompanionEntry;
using micronotes::library::FolderNode;
using micronotes::library::NoteListItem;
using micronotes::ui::TreeModel;
using micronotes::ui::TreeRow;
using micronotes::ui::TreeRowKind;

const std::filesystem::path kRoot = "/library/Notes";

std::vector<FolderNode> fixtureFolders() {
  return {{"", 1}, {"work", 2}, {"work/2026", 1}, {"ideas", 0}};
}

// `folder` is the library-relative directory the note is filed in, and the tree
// reads it rather than re-deriving it from the path, so a fixture that leaves it
// empty is a fixture where every note is at the root.
std::vector<NoteListItem> fixtureNotes() {
  return {
    {"n-root", kRoot / "Inbox.md", "Inbox", {}, "", ""},
    {"n-a", kRoot / "work" / "Alpha.md", "Alpha", {}, "\xF0\x9F\x93\x93", "work"},
    {"n-b", kRoot / "work" / "Beta.md", "Beta", {}, "", "work"},
    {"n-c", kRoot / "work" / "2026" / "Plan.md", "Plan", {}, "", "work/2026"},
  };
}

// A files directory as `<files>` and a companion as `name (file)`, so a shape
// says which kind every row is.
std::string shape(const std::vector<TreeRow>& rows) {
  std::string out;
  for(const auto& row : rows) {
    out += std::string(static_cast<std::size_t>(row.depth) * 2, ' ');
    switch(row.kind) {
      case TreeRowKind::Folder: out += "[" + row.label + "]"; break;
      case TreeRowKind::FilesFolder: out += "<" + row.label + ">"; break;
      case TreeRowKind::File: out += row.label + " (file)"; break;
      case TreeRowKind::Note: out += row.label; break;
    }
    out += "\n";
  }
  return out;
}

}

// The tree starts at the library root's *contents*. The root had a row of its
// own, labelled with the library directory's name -- so a library at `~/Notes`
// grew a `[Notes]` node that everything else hung under, which reads as a
// folder somebody added and is a container inside a container: the sidebar's
// Notebooks band already names the section and already collapses it.
//
// The visible payoff is the indent: every row is one step further left, and the
// top level sits at depth 0 where it used to sit at 1.
MICRONOTES_TEST(tree_starts_at_the_library_contents_not_at_a_root_row) {
  TreeModel tree;
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes())) ==
                     "[ideas]\n"
                     "[work]\n"
                     "Inbox\n");

  tree.setExpanded("work", true);
  // Folders before notes at every level, each sorted by name.
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes())) ==
                     "[ideas]\n"
                     "[work]\n"
                     "  [2026]\n"
                     "  Alpha\n"
                     "  Beta\n"
                     "Inbox\n");
}

// The root is always open, because there is no row to close it with. Asking is
// still allowed -- `reveal` walks from the root and would otherwise need a
// special case -- and the answer never changes.
MICRONOTES_TEST(tree_treats_the_root_as_permanently_open) {
  TreeModel tree;
  MICRONOTES_REQUIRE(tree.expanded(""));
  // Setting it is a no-op rather than an error, and does not dirty the file:
  // there is nothing about the root left to persist.
  tree.setExpanded("", false);
  MICRONOTES_REQUIRE(tree.expanded(""));
  MICRONOTES_REQUIRE(!tree.dirty());
  MICRONOTES_REQUIRE(tree.toggle("") == true);
  MICRONOTES_REQUIRE(tree.expanded(""));
  // And the tree is still there, which is the thing that used to be closable.
  MICRONOTES_REQUIRE(!tree.rows(fixtureFolders(), fixtureNotes()).empty());
}

MICRONOTES_TEST(tree_reveal_opens_every_ancestor) {
  TreeModel tree;
  tree.reveal("work/2026");
  const auto rows = tree.rows(fixtureFolders(), fixtureNotes());
  MICRONOTES_REQUIRE(shape(rows) ==
                     "[ideas]\n"
                     "[work]\n"
                     "  [2026]\n"
                     "    Plan\n"
                     "  Alpha\n"
                     "  Beta\n"
                     "Inbox\n");
  // A folder with nothing in it offers no disclosure control to click.
  for(const auto& row : rows) {
    if(row.label == "ideas") MICRONOTES_REQUIRE(!row.expandable);
    if(row.label == "work") MICRONOTES_REQUIRE(row.expandable && row.expanded);
  }
}

MICRONOTES_TEST(tree_expansion_round_trips_through_its_file) {
  TreeModel tree;
  tree.reveal("work/2026");
  MICRONOTES_REQUIRE(tree.dirty());

  TreeModel reloaded;
  reloaded.load(tree.serialize());
  MICRONOTES_REQUIRE(!reloaded.dirty());
  MICRONOTES_REQUIRE(reloaded.expanded("work"));
  MICRONOTES_REQUIRE(reloaded.expanded("work/2026"));
  MICRONOTES_REQUIRE(!reloaded.expanded("ideas"));
}

// A `tree.state` written when the root had a row could carry `!root` to say it
// was closed. That line has to be read and dropped rather than taken for a
// folder named "!root", which would otherwise sit in the expanded set forever
// and be written back out on every save.
MICRONOTES_TEST(tree_drops_the_collapsed_root_line_an_older_file_may_carry) {
  TreeModel tree;
  tree.load("!root\nwork\n");
  MICRONOTES_REQUIRE(tree.expanded("work"));
  MICRONOTES_REQUIRE(!tree.expanded("!root"));
  // And it is gone from the file the next save writes.
  MICRONOTES_REQUIRE(tree.serialize() == "work\n");
  // The tree it used to hide is showing, which is the point.
  MICRONOTES_REQUIRE(!tree.rows(fixtureFolders(), fixtureNotes()).empty());
}

MICRONOTES_TEST(tree_carries_the_note_icon_and_folder_counts) {
  TreeModel tree;
  tree.setExpanded("work", true);
  for(const auto& row : tree.rows(fixtureFolders(), fixtureNotes())) {
    if(row.label == "Alpha") MICRONOTES_REQUIRE(row.icon == "\xF0\x9F\x93\x93");
    if(row.label == "Beta") MICRONOTES_REQUIRE(row.icon.empty());
    if(row.kind == TreeRowKind::Folder && row.label == "work") MICRONOTES_REQUIRE(row.noteCount == 2);
  }
}

namespace {

// Every companion the walk would report for the fixture, deliberately out of
// order: the tree sorts, the list does not have to.
std::vector<CompanionEntry> fixtureCompanions() {
  return {
    {"work/files/diagram.png", "work/files", false},
    {"work/files", "work", true},
    {"work/files/sub", "work/files", true},
    {"work/files/sub/clip.mp4", "work/files/sub", false},
    {"work/files/archive.zip", "work/files", false},
    {"files", "", true},
    {"files/report.pdf", "files", false},
    // A notebook with nothing but a files directory in it.
    {"ideas/files", "ideas", true},
  };
}

}

// A notebook's `files/` sits between its notebooks and its notes; inside it,
// folders come before files and both sort by name. A companion row carries its
// own path in `file` and its parent in `folder`, the way a note row carries its
// notebook.
MICRONOTES_TEST(tree_lists_a_notebooks_files_between_its_notebooks_and_its_notes) {
  TreeModel tree;
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes(), fixtureCompanions())) ==
                     "[ideas]\n"
                     "[work]\n"
                     "<files>\n"
                     "Inbox\n");

  tree.reveal("work/files/sub");
  const auto rows = tree.rows(fixtureFolders(), fixtureNotes(), fixtureCompanions());
  MICRONOTES_REQUIRE(shape(rows) ==
                     "[ideas]\n"
                     "[work]\n"
                     "  [2026]\n"
                     "  <files>\n"
                     "    <sub>\n"
                     "      clip.mp4 (file)\n"
                     "    archive.zip (file)\n"
                     "    diagram.png (file)\n"
                     "  Alpha\n"
                     "  Beta\n"
                     "<files>\n"
                     "Inbox\n");
  for(const auto& row : rows) {
    if(row.kind == TreeRowKind::FilesFolder && row.file == std::filesystem::path("work/files")) {
      MICRONOTES_REQUIRE(row.folder == std::filesystem::path("work"));
      MICRONOTES_REQUIRE(row.noteCount == 3);  // sub, archive.zip, diagram.png
      MICRONOTES_REQUIRE(row.expandable && row.expanded);
    }
    if(row.kind == TreeRowKind::File && row.label == "clip.mp4") {
      MICRONOTES_REQUIRE(row.file == std::filesystem::path("work/files/sub/clip.mp4"));
      MICRONOTES_REQUIRE(row.folder == std::filesystem::path("work/files/sub"));
    }
    // A notebook holding only a files directory still has something to unfold.
    if(row.kind == TreeRowKind::Folder && row.label == "ideas") MICRONOTES_REQUIRE(row.expandable);
    // And the root's own files directory is a top-level row with an empty parent.
    if(row.kind == TreeRowKind::FilesFolder && row.file == std::filesystem::path("files")) {
      MICRONOTES_REQUIRE(row.folder.empty());
      MICRONOTES_REQUIRE(row.depth == 0);
      MICRONOTES_REQUIRE(!row.expanded);
    }
  }
}
