#include "TestSupport.h"

#include "ui/TreeModel.h"

#include <string>
#include <vector>

namespace {

using micronotes::library::FolderNode;
using micronotes::library::NoteListItem;
using micronotes::ui::TreeModel;
using micronotes::ui::TreeRow;
using micronotes::ui::TreeRowKind;

const std::filesystem::path kRoot = "/library/Notes";

std::vector<FolderNode> fixtureFolders() {
  return {{"", 1}, {"work", 2}, {"work/2026", 1}, {"ideas", 0}};
}

std::vector<NoteListItem> fixtureNotes() {
  return {
    {"n-root", kRoot / "Inbox.md", "Inbox", {}, ""},
    {"n-a", kRoot / "work" / "Alpha.md", "Alpha", {}, "\xF0\x9F\x93\x93"},
    {"n-b", kRoot / "work" / "Beta.md", "Beta", {}, ""},
    {"n-c", kRoot / "work" / "2026" / "Plan.md", "Plan", {}, ""},
  };
}

std::string shape(const std::vector<TreeRow>& rows) {
  std::string out;
  for(const auto& row : rows) {
    out += std::string(static_cast<std::size_t>(row.depth) * 2, ' ');
    out += row.kind == TreeRowKind::Folder ? "[" + row.label + "]" : row.label;
    out += "\n";
  }
  return out;
}

}

MICRONOTES_TEST(tree_shows_only_what_is_expanded) {
  TreeModel tree;
  // Root open, everything under it closed: one row per top-level entry.
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes(), kRoot)) ==
                     "[Notes]\n"
                     "  [ideas]\n"
                     "  [work]\n"
                     "  Inbox\n");

  tree.setExpanded("work", true);
  // Folders before notes at every level, each sorted by name.
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes(), kRoot)) ==
                     "[Notes]\n"
                     "  [ideas]\n"
                     "  [work]\n"
                     "    [2026]\n"
                     "    Alpha\n"
                     "    Beta\n"
                     "  Inbox\n");

  MICRONOTES_REQUIRE(tree.toggle("") == false);
  MICRONOTES_REQUIRE(shape(tree.rows(fixtureFolders(), fixtureNotes(), kRoot)) == "[Notes]\n");
}

MICRONOTES_TEST(tree_reveal_opens_every_ancestor) {
  TreeModel tree;
  tree.setExpanded("", false);
  tree.reveal("work/2026");
  const auto rows = tree.rows(fixtureFolders(), fixtureNotes(), kRoot);
  MICRONOTES_REQUIRE(shape(rows) ==
                     "[Notes]\n"
                     "  [ideas]\n"
                     "  [work]\n"
                     "    [2026]\n"
                     "      Plan\n"
                     "    Alpha\n"
                     "    Beta\n"
                     "  Inbox\n");
  // A folder with nothing in it offers no disclosure control to click.
  for(const auto& row : rows) {
    if(row.label == "ideas") MICRONOTES_REQUIRE(!row.expandable);
    if(row.label == "work") MICRONOTES_REQUIRE(row.expandable && row.expanded);
  }
}

MICRONOTES_TEST(tree_expansion_round_trips_through_its_file) {
  TreeModel tree;
  tree.reveal("work/2026");
  tree.setExpanded("", false);
  MICRONOTES_REQUIRE(tree.dirty());

  TreeModel reloaded;
  reloaded.load(tree.serialize());
  MICRONOTES_REQUIRE(!reloaded.dirty());
  MICRONOTES_REQUIRE(!reloaded.expanded(""));
  MICRONOTES_REQUIRE(reloaded.expanded("work"));
  MICRONOTES_REQUIRE(reloaded.expanded("work/2026"));
  MICRONOTES_REQUIRE(!reloaded.expanded("ideas"));
}

MICRONOTES_TEST(tree_carries_the_note_icon_and_folder_counts) {
  TreeModel tree;
  tree.setExpanded("work", true);
  for(const auto& row : tree.rows(fixtureFolders(), fixtureNotes(), kRoot)) {
    if(row.label == "Alpha") MICRONOTES_REQUIRE(row.icon == "\xF0\x9F\x93\x93");
    if(row.label == "Beta") MICRONOTES_REQUIRE(row.icon.empty());
    if(row.kind == TreeRowKind::Folder && row.label == "work") MICRONOTES_REQUIRE(row.noteCount == 2);
  }
}
