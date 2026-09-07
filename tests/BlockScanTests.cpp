#include "TestSupport.h"

#include <type_traits>

#include "doc/BlockScan.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    micronotes::tests::require(block.end() > block.start || source.empty(), label + ": empty block at " + std::to_string(block.start));
    micronotes::tests::require(block.contentStart() >= block.start && block.contentEnd() <= block.end(),
                               label + ": content escapes block at " + std::to_string(block.start));
    micronotes::tests::require(block.contentStart() <= block.contentEnd(), label + ": inverted content range");
    expected = block.end();
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
  MICRONOTES_REQUIRE(blocks[0].info(source) == "sh");
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart(), blocks[0].contentEnd() - blocks[0].contentStart()) == "# not a heading\n");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Heading);
  MICRONOTES_REQUIRE(blocks[1].level == 1);
  MICRONOTES_REQUIRE(source.substr(blocks[1].contentStart(), blocks[1].contentEnd() - blocks[1].contentStart()) == "real heading");
}

MICRONOTES_TEST(block_scan_records_list_markers_and_depth) {
  const std::string source = "- top\n  - nested\n    - deeper\n1. first\n7) seventh\n- [x] done\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 6);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Bullet && blocks[0].listDepth == 0);
  MICRONOTES_REQUIRE(blocks[1].listDepth == 1);
  MICRONOTES_REQUIRE(blocks[2].listDepth == 2);
  MICRONOTES_REQUIRE(source.substr(blocks[2].contentStart(), blocks[2].contentEnd() - blocks[2].contentStart()) == "deeper");
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
  MICRONOTES_REQUIRE(blocks[0].info(source) == "NOTE");
  MICRONOTES_REQUIRE(blocks[1].kind == BlockKind::Quote);
  MICRONOTES_REQUIRE(source.substr(blocks[1].contentStart(), blocks[1].contentEnd() - blocks[1].contentStart()) == "body");
  MICRONOTES_REQUIRE(blocks[3].kind == BlockKind::Divider);
  MICRONOTES_REQUIRE(blocks[5].kind == BlockKind::Divider);
}

MICRONOTES_TEST(block_scan_merges_wrapped_paragraph_lines) {
  const std::string source = "one\ntwo\nthree\n\nnext\n";
  const auto blocks = scanBlocks(source);
  MICRONOTES_REQUIRE(blocks.size() == 3);
  MICRONOTES_REQUIRE(blocks[0].kind == BlockKind::Paragraph);
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart(), blocks[0].contentEnd() - blocks[0].contentStart()) == "one\ntwo\nthree");
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
  MICRONOTES_REQUIRE(source.substr(blocks[0].contentStart(), blocks[0].contentEnd() - blocks[0].contentStart()) == "one\n  still one");
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
    MICRONOTES_REQUIRE(offset < blocks[index].end() || index + 1 == blocks.size());
  }
}

// The property the incremental rescan rests on: the scanner carries nothing
// between blocks, so a scan resumed at any block boundary produces exactly the
// blocks a scan from the top produces from there on.
//
// It holds today because no branch of `scanOneBlock` reads a byte before its own
// `pos` -- a paragraph absorbs the lines *after* it, a fence closes on a later
// line, a table is decided by the row under it. Nothing looks up. This asserts
// it at every boundary of a document with one of everything in it, so a
// construct added later that does look back fails here rather than by leaving a
// stale block on screen after an edit three paragraphs above it.
MICRONOTES_TEST(blockscan_resuming_at_every_boundary_matches_a_full_scan) {
  const auto sameBlock = [](std::string_view text, const SourceBlock& a, const SourceBlock& b) {
    return a.kind == b.kind && a.start == b.start && a.end() == b.end() &&
           a.contentStart() == b.contentStart() && a.contentEnd() == b.contentEnd() &&
           a.level == b.level && a.listDepth == b.listDepth && a.ordinal == b.ordinal &&
           a.listMarker == b.listMarker && a.checked == b.checked && a.info(text) == b.info(text);
  };

  for(const std::string& source :
      {readFixture("docs/markdown-elements.md"),
       std::string("# h\n\nprose over\ntwo lines\n\n- item\n  more\n\n```cpp\nint x;\n```\n\n"
                   "| a | b |\n|:--|--:|\n| 1 | 2 |\n\n> [!NOTE] title\n> body\n\n---\n\n"
                   "    indented code\n    second line\n\n1. one\n2. two\n"),
       std::string("```unclosed\nstill inside\n"), std::string("no trailing newline"),
       std::string("\n\n\n")}) {
    const auto full = scanBlocks(source);
    MICRONOTES_REQUIRE(!full.empty());
    for(std::size_t at = 0; at < full.size(); ++at) {
      std::vector<SourceBlock> resumed;
      const std::size_t stopped =
        micronotes::doc::scanBlocksFrom(source, full[at].start, 0, {}, &resumed);
      micronotes::tests::require(stopped == source.size(),
                                 "resumed scan stopped early at block " + std::to_string(at));
      micronotes::tests::require(resumed.size() == full.size() - at,
                                 "resumed scan produced " + std::to_string(resumed.size()) +
                                   " blocks from block " + std::to_string(at) + " of " +
                                   std::to_string(full.size()));
      for(std::size_t i = 0; i < resumed.size(); ++i) {
        micronotes::tests::require(sameBlock(source, resumed[i], full[at + i]),
                                   "resumed block " + std::to_string(i) + " differs from full scan "
                                   "block " + std::to_string(at + i));
      }
    }
  }
}

// The stop condition, on its own terms: given a tail of bytes the caller knows
// is unchanged and a predicate that recognises the previous scan's boundaries,
// the scan stops at the first one and leaves the rest to the caller.
MICRONOTES_TEST(blockscan_stops_at_the_first_boundary_inside_the_untouched_tail) {
  const std::string source = "# one\n\npara two\n\n- three\n\npara four\n";
  const auto full = scanBlocks(source);
  MICRONOTES_REQUIRE(full.size() >= 5);
  const std::size_t third = full[2].start;

  std::vector<SourceBlock> resumed;
  const std::size_t stopped = micronotes::doc::scanBlocksFrom(
    source, 0, source.size() - third,
    [&](std::size_t offset) {
      return micronotes::doc::blockIndexAt(full, offset) < full.size() &&
             full[micronotes::doc::blockIndexAt(full, offset)].start == offset;
    },
    &resumed);
  // The tail begins at the third block, so the first boundary inside it is the
  // third block's start -- and the scan hands back the two blocks above it.
  MICRONOTES_REQUIRE(stopped == third);
  MICRONOTES_REQUIRE(resumed.size() == 2);
  MICRONOTES_REQUIRE(resumed.back().end() == third);
}

// A block is the unit the whole layout moves around: a block-count change
// memmoves the tail of the array (9,612 blocks for a 200 KB note) and every
// edit shifts the tail's `start`. It was 88 bytes -- four absolute `size_t` and
// an owned `std::string` for a fence language almost no block has -- and that
// is the memmove and the cache traffic, not an aesthetic. Asserted so it cannot
// grow back without somebody deciding to.
MICRONOTES_TEST(block_scan_source_block_stays_small) {
  static_assert(sizeof(SourceBlock) <= 40, "SourceBlock grew past 40 bytes");
  static_assert(std::is_trivially_copyable_v<SourceBlock>,
                "SourceBlock must stay trivially copyable: the splice memmoves it");
  MICRONOTES_REQUIRE(sizeof(SourceBlock) <= 40);
}
