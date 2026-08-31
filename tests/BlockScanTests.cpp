#include "TestSupport.h"

#include "doc/BlockScan.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using micronotes::doc::BlockKind;
using micronotes::doc::SourceBlock;
using micronotes::doc::scanBlocks;

namespace {

// The invariant the whole layout cache rests on.
void requirePartitions(const std::string& source, const std::string& label) {
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(!blocks.empty());
  std::size_t expected = 0;
  for(const auto& block : blocks) {
    micronotes::tests::require(block.start == expected, label + ": gap or overlap at " + std::to_string(block.start));
    micronotes::tests::require(block.end > block.start || source.empty(), label + ": empty block at " + std::to_string(block.start));
    micronotes::tests::require(block.contentStart >= block.start && block.contentEnd <= block.end,
                               label + ": content escapes block at " + std::to_string(block.start));
    micronotes::tests::require(block.contentStart <= block.contentEnd, label + ": inverted content range");
    expected = block.end;
  }
  micronotes::tests::require(expected == source.size(), label + ": blocks stop at " + std::to_string(expected) +
                                                            " of " + std::to_string(source.size()));
}

std::string readFixture(const char* relative) {
  const std::filesystem::path path = std::filesystem::path(MICRONOTES_SOURCE_DIR) / relative;
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

}

MICRONOTES_TEST(block_scan_partitions_the_documentation_fixture) {
  const auto source = readFixture("docs/markdown-elements.md");
  MICRONOTES_REQUIRE(source.size() > 500);
  requirePartitions(source, "markdown-elements");
}

MICRONOTES_TEST(block_scan_partitions_generated_inputs) {
  const char* fragments[] = {
    "# Heading\n", "\n", "Paragraph text\nwrapped onto two lines\n", "- bullet\n",
    "- [ ] todo\n", "- [x] done\n", "1. ordered\n", "> quote\n", "> [!NOTE]\n",
    "```cpp\nint main() { return 0; }\n```\n", "---\n", "| a | b |\n|---|---|\n| 1 | 2 |\n",
    "<div>raw</div>\n", "[^note]: a footnote\n", "    indented continuation\n", "no trailing newline",
  };
  const std::size_t count = sizeof(fragments) / sizeof(fragments[0]);
  for(std::size_t seed = 0; seed < 400; ++seed) {
    std::string source;
    std::size_t state = seed * 2654435761u + 1;
    const std::size_t pieces = 1 + seed % 7;
    for(std::size_t i = 0; i < pieces; ++i) {
      state = state * 6364136223846793005ull + 1442695040888963407ull;
      source += fragments[(state >> 33) % count];
    }
    requirePartitions(source, "generated seed " + std::to_string(seed));
  }
}

MICRONOTES_TEST(block_scan_keeps_hashes_inside_a_fence_out_of_headings) {
  const std::string source = "```sh\n# not a heading\n```\n# real heading\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 2);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Code);
  MICRONOTES_REQUIRE(blocks[0].info == "sh");
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart, blocks[0].contentEnd - blocks[0].contentStart) == "# not a heading\n");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Heading);
  MICRONOTES_REQUIRE(blocks[1].level == 1);
  MICRONOTES_REQUIRE(source.substr(blocks[1].contentStart, blocks[1].contentEnd - blocks[1].contentStart) == "real heading");
}

MICRONOTES_TEST(block_scan_records_list_markers_and_depth) {
  const std::string source = "- top\n  - nested\n    - deeper\n1. first\n7) seventh\n- [x] done\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 6);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Bullet && blocks[0].listDepth == 0);
  MICRONOTES_REQUIRE(blocks[1].listDepth == 1);
  MICRONOTES_REQUIRE(blocks[2].listDepth == 2);
  MICRONOTES_REQUIRE(source.substr(blocks[2].contentStart, blocks[2].contentEnd - blocks[2].contentStart) == "deeper");
  MICRONOTES_REQUIRE(blocks[3].kind == BlockKind::Ordered && blocks[3].ordinal == 1);
  MICRONOTES_REQUIRE(blocks[4].kind == BlockKind::Ordered && blocks[4].ordinal == 7);
  MICRONOTES_REQUIRE(blocks[5].kind == BlockKind::Todo && blocks[5].checked);
}

MICRONOTES_TEST(block_scan_tags_tables_html_and_footnotes_complex) {
  const std::string source = "| a | b |\n|---|---|\n| 1 | 2 |\n\n<div>\nraw\n</div>\n\n[^x]: note text\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 5);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Complex);
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Blank);
  MICRONOTES_REQUIRE(blocks[2].kind == BlockKind::Complex);
  MICRONOTES_REQUIRE(blocks[4].kind == BlockKind::Complex);
  requirePartitions(source, "complex mix");
}

MICRONOTES_TEST(block_scan_separates_quotes_callouts_and_dividers) {
  const std::string source = "> [!NOTE]\n> body\n\n---\n\n***\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Callout);
  MICRONOTES_REQUIRE(blocks[0].info == "NOTE");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Quote);
  MICRONOTES_REQUIRE(source.substr(blocks[1].contentStart, blocks[1].contentEnd - blocks[1].contentStart) == "body");
  MICRONOTES_REQUIRE(blocks[3].kind == BlockKind::Divider);
  MICRONOTES_REQUIRE(blocks[5].kind == BlockKind::Divider);
}

MICRONOTES_TEST(block_scan_merges_wrapped_paragraph_lines) {
  const std::string source = "one\ntwo\nthree\n\nnext\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 3);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Paragraph);
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart, blocks[0].contentEnd - blocks[0].contentStart) == "one\ntwo\nthree");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Blank);
}

MICRONOTES_TEST(block_scan_merges_wrapped_list_item_lines) {
  // A hand-wrapped item is one item, on the same terms as a hand-wrapped
  // paragraph. As several blocks it drew a line break and the continuation's
  // own indentation into the middle of the sentence.
  const std::string source = "- one\n  still one\n- two\n\ntail\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 4);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Bullet);
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart, blocks[0].contentEnd - blocks[0].contentStart) == "one\n  still one");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Bullet);
  MICRONOTES_REQUIRE(blocks[2].kind == BlockKind::Blank);
  requirePartitions(source, "wrapped list item");
}

MICRONOTES_TEST(block_scan_does_not_let_an_item_swallow_a_deeply_nested_one) {
  // Four columns of indentation would otherwise read as code, and the item
  // above would absorb it. Both the scan loop and the "does this start a
  // block?" test have to resolve that the same way or the two disagree.
  const std::string source = "- top\n    - deep\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 2);
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Bullet);
  MICRONOTES_REQUIRE(blocks[1].listDepth == 2);
  requirePartitions(source, "deeply nested item");
}

MICRONOTES_TEST(block_scan_maps_offsets_back_to_blocks) {
  const std::string source = "# a\n\n- b\n";
  const auto blocks = scanBlocks(source);
  for(std::size_t offset = 0; offset <= source.size(); ++offset) {
    const auto index = micronotes::doc::blockIndexAt(blocks, offset);
    MICRONOTES_REQUIRE(index < blocks.size());
    MICRONOTES_REQUIRE(blocks[index].start <= offset);
    MICRONOTES_REQUIRE(offset < blocks[index].end || index + 1 == blocks.size());
  }
}
