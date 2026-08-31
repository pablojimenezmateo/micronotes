#include "TestSupport.h"

#include "doc/BlockScan.h"
#include "doc/Fold.h"
#include "ui/FoldState.h"

#include <filesystem>
#include <string>

using micronotes::doc::BlockKind;
using micronotes::doc::foldEnd;
using micronotes::doc::foldKey;
using micronotes::doc::foldable;
using micronotes::doc::scanBlocks;

namespace {

std::size_t indexOfKind(const std::vector<micronotes::doc::SourceBlock>& blocks, BlockKind kind, int nth = 0) {
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    if(blocks[i].kind == kind && nth-- == 0) return i;
  }
  return blocks.size();
}

}

MICRONOTES_TEST(fold_a_heading_owns_its_section) {
  const std::string source =
      "# Title\n\nIntro\n\n## Build\n\nRun cmake\n\n### Notes\n\nA detail\n\n## Ship\n\nTag it\n";
  const auto blocks = scanBlocks(source);

  const std::size_t build = indexOfKind(blocks, BlockKind::Heading, 1);
  MICRONOTES_REQUIRE(build < blocks.size());
  MICRONOTES_REQUIRE(foldable(blocks, build));
  // Everything down to the next heading of the same rank, nested ones included.
  const std::size_t end = foldEnd(blocks, build);
  const std::size_t ship = indexOfKind(blocks, BlockKind::Heading, 3);
  MICRONOTES_REQUIRE(end <= ship);
  MICRONOTES_REQUIRE(end > build + 1);
  // The blank line before the next heading belongs to neither section.
  MICRONOTES_REQUIRE(blocks[end - 1].kind != BlockKind::Blank);

  // A heading with nothing under it has nothing to hide.
  const std::size_t last = indexOfKind(blocks, BlockKind::Heading, 3);
  MICRONOTES_REQUIRE(foldable(blocks, last));
  const std::string trailing = "# Title\n\n## Empty\n";
  const auto tail = scanBlocks(trailing);
  MICRONOTES_REQUIRE(!foldable(tail, indexOfKind(tail, BlockKind::Heading, 1)));
}

MICRONOTES_TEST(fold_a_list_item_owns_what_is_indented_under_it) {
  const std::string source = "- one\n  - nested\n  - also nested\n- two\n";
  const auto blocks = scanBlocks(source);

  MICRONOTES_REQUIRE(foldable(blocks, 0));
  const std::size_t end = foldEnd(blocks, 0);
  MICRONOTES_REQUIRE(end == 3);
  MICRONOTES_REQUIRE(blocks[3].listDepth == 0);
  // The nested items own nothing, and neither does the last sibling.
  MICRONOTES_REQUIRE(!foldable(blocks, 1));
  MICRONOTES_REQUIRE(!foldable(blocks, 3));
}

MICRONOTES_TEST(fold_leaves_paragraphs_and_quotes_alone) {
  const std::string source = "Just text\n\n> quoted\n> more\n\n```\ncode\n```\n";
  const auto blocks = scanBlocks(source);
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    if(blocks[i].kind == BlockKind::Heading || micronotes::doc::isListKind(blocks[i].kind)) continue;
    micronotes::tests::require(!foldable(blocks, i), "block " + std::to_string(i) + " should not fold");
  }
}

MICRONOTES_TEST(fold_keys_survive_edits_elsewhere) {
  const auto before = scanBlocks("# Title\n\n## Build the thing\n\nstep\n");
  const auto after = scanBlocks("# Title\n\nA new paragraph.\n\n## Build the thing\n\nstep\n");
  const std::string key = foldKey("# Title\n\n## Build the thing\n\nstep\n", before[indexOfKind(before, BlockKind::Heading, 1)]);
  const std::string moved = foldKey("# Title\n\nA new paragraph.\n\n## Build the thing\n\nstep\n",
                                    after[indexOfKind(after, BlockKind::Heading, 1)]);
  MICRONOTES_REQUIRE(key == moved);
  MICRONOTES_REQUIRE(key == "h2:Build the thing");

  // Rank and depth are part of the identity: promoting a heading forgets its
  // fold rather than applying it to a different shape.
  const auto promoted = scanBlocks("# Build the thing\n\nstep\n");
  MICRONOTES_REQUIRE(foldKey("# Build the thing\n\nstep\n", promoted[0]) == "h1:Build the thing");
  // No key may carry the separator the fold file is written with.
  const auto wrapped = scanBlocks("- one\n  two\n");
  MICRONOTES_REQUIRE(foldKey("- one\n  two\n", wrapped[0]).find('\t') == std::string::npos);
  MICRONOTES_REQUIRE(foldKey("- one\n  two\n", wrapped[0]).find('\n') == std::string::npos);
}

MICRONOTES_TEST(fold_state_round_trips_through_its_file) {
  const auto path = std::filesystem::temp_directory_path() / "micronotes-fold-test.state";
  std::filesystem::remove(path);

  micronotes::ui::FoldState state;
  MICRONOTES_REQUIRE(state.toggle("note-a", "h2:Build the thing"));
  MICRONOTES_REQUIRE(state.toggle("note-a", "li0:steps"));
  MICRONOTES_REQUIRE(state.toggle("note-b", "h1:Other"));
  MICRONOTES_REQUIRE(!state.toggle("note-a", "li0:steps"));
  MICRONOTES_REQUIRE(state.save(path));

  micronotes::ui::FoldState reloaded;
  MICRONOTES_REQUIRE(reloaded.load(path));
  MICRONOTES_REQUIRE(reloaded.folded("note-a", "h2:Build the thing"));
  MICRONOTES_REQUIRE(!reloaded.folded("note-a", "li0:steps"));
  MICRONOTES_REQUIRE(reloaded.folded("note-b", "h1:Other"));
  MICRONOTES_REQUIRE(!reloaded.folded("note-c", "h1:Other"));

  reloaded.clearNote("note-a");
  MICRONOTES_REQUIRE(!reloaded.folded("note-a", "h2:Build the thing"));
  MICRONOTES_REQUIRE(!reloaded.empty());
  std::filesystem::remove(path);
}
