#include "TestSupport.h"

#include "editor/MarkdownEditor.h"
#include "editor/SoftWrap.h"
#include "markdown/MarkdownParser.h"
#include "ui/ShellModel.h"

#include <filesystem>
#include <fstream>
#include <sstream>

MICRONOTES_TEST(editor_tracks_dirty_state) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("hello");
  MICRONOTES_REQUIRE(!editor.dirty());
  editor.insert(" world");
  MICRONOTES_REQUIRE(editor.dirty());
  MICRONOTES_REQUIRE(editor.text() == "hello world");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "hello");
  MICRONOTES_REQUIRE(editor.redo());
  MICRONOTES_REQUIRE(editor.text() == "hello world");
  editor.markSaved();
  MICRONOTES_REQUIRE(!editor.dirty());
}

MICRONOTES_TEST(editor_moves_cursor_and_deletes_forward) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("one\ntwo\nthree");
  editor.moveCursor(5);
  editor.moveLineDown();
  MICRONOTES_REQUIRE(editor.cursor() == 9);
  editor.moveLineUp();
  MICRONOTES_REQUIRE(editor.cursor() == 5);
  editor.moveLeft();
  editor.eraseNext();
  MICRONOTES_REQUIRE(editor.text() == "one\nwo\nthree");
}

MICRONOTES_TEST(editor_moves_and_erases_by_utf8_codepoints) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("caf\xC3\xA9");  // "café", the 'é' is two bytes
  MICRONOTES_REQUIRE(editor.cursor() == 5);
  editor.moveLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 3);  // landed on the codepoint start, not mid-byte
  editor.moveRight();
  MICRONOTES_REQUIRE(editor.cursor() == 5);
  editor.erasePrevious();
  MICRONOTES_REQUIRE(editor.text() == "caf");  // whole 'é' removed, no dangling byte
  editor.setText("\xC3\xA9xy");  // "éxy"
  editor.moveCursor(0);
  editor.eraseNext();
  MICRONOTES_REQUIRE(editor.text() == "xy");  // whole leading 'é' removed
}

MICRONOTES_TEST(editor_moves_to_line_boundaries) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("one\ntwo three\nfour");
  editor.moveCursor(8);
  editor.moveLineStart();
  MICRONOTES_REQUIRE(editor.cursor() == 4);
  editor.moveLineEnd();
  MICRONOTES_REQUIRE(editor.cursor() == 13);
  editor.moveCursor(8);
  editor.moveLineStart(true);
  MICRONOTES_REQUIRE(editor.selectedText() == "two ");
}

MICRONOTES_TEST(editor_selects_replaces_and_erases_ranges) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha beta");
  editor.selectRange(6, 10);
  MICRONOTES_REQUIRE(editor.hasSelection());
  MICRONOTES_REQUIRE(editor.selectedText() == "beta");
  editor.insert("gamma");
  MICRONOTES_REQUIRE(editor.text() == "alpha gamma");
  editor.selectAll();
  MICRONOTES_REQUIRE(editor.selectedText() == "alpha gamma");
  editor.eraseSelection();
  MICRONOTES_REQUIRE(editor.text().empty());
}

MICRONOTES_TEST(editor_ignores_empty_insert_without_selection) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha");
  editor.insert("");
  MICRONOTES_REQUIRE(!editor.dirty());
  MICRONOTES_REQUIRE(!editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "alpha");
}

MICRONOTES_TEST(editor_caps_undo_history) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("");
  for(int i = 0; i < 105; ++i) {
    editor.insert("x");
  }
  int undoCount = 0;
  while(editor.undo()) {
    ++undoCount;
  }
  MICRONOTES_REQUIRE(undoCount == 100);
  MICRONOTES_REQUIRE(editor.text().size() == 5);
}

MICRONOTES_TEST(editor_soft_wraps_by_words_without_changing_source_offsets) {
  const std::string source = "alpha beta gamma";
  const auto rows = micronotes::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(rows[0].text == "alpha beta ");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 11);
  MICRONOTES_REQUIRE(rows[1].text == "gamma");
  MICRONOTES_REQUIRE(rows[1].start == 11);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_keeps_remaining_words_together_when_they_fit) {
  const std::string source = "alpha beta gamma delta";
  const auto rows = micronotes::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(rows[0].text == "alpha beta ");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 11);
  MICRONOTES_REQUIRE(rows[1].text == "gamma delta");
  MICRONOTES_REQUIRE(rows[1].start == 11);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_preserves_hard_newlines) {
  const std::string source = "one two\nthree";
  const auto rows = micronotes::editor::softWrap(source, 20, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 2);
  MICRONOTES_REQUIRE(rows[0].text == "one two");
  MICRONOTES_REQUIRE(rows[0].start == 0);
  MICRONOTES_REQUIRE(rows[0].end == 7);
  MICRONOTES_REQUIRE(rows[1].text == "three");
  MICRONOTES_REQUIRE(rows[1].start == 8);
  MICRONOTES_REQUIRE(rows[1].end == source.size());
}

MICRONOTES_TEST(editor_soft_wrap_splits_oversized_words) {
  const std::string source = "abcdefgh";
  const auto rows = micronotes::editor::softWrap(source, 3, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 3);
  MICRONOTES_REQUIRE(rows[0].text == "abc");
  MICRONOTES_REQUIRE(rows[1].text == "def");
  MICRONOTES_REQUIRE(rows[2].text == "gh");
}

MICRONOTES_TEST(editor_soft_wrap_keeps_utf8_codepoints_intact) {
  const std::string source = "a\xC3\xA9\xC3\xA9";  // "aéé", each 'é' is two bytes
  const auto rows = micronotes::editor::softWrap(source, 2, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICRONOTES_REQUIRE(rows.size() == 3);
  MICRONOTES_REQUIRE(rows[0].text == "a");
  MICRONOTES_REQUIRE(rows[1].text == "\xC3\xA9");
  MICRONOTES_REQUIRE(rows[2].text == "\xC3\xA9");
}

MICRONOTES_TEST(editor_soft_wrap_maps_offsets_and_hit_testing) {
  const std::string source = "alpha beta gamma";
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  const auto rows = micronotes::editor::softWrap(source, 11, measure);
  MICRONOTES_REQUIRE(micronotes::editor::rowForOffset(rows, 0) == 0);
  MICRONOTES_REQUIRE(micronotes::editor::rowForOffset(rows, 12) == 1);
  MICRONOTES_REQUIRE(micronotes::editor::offsetForRowX(rows[1], 2.0f, measure) == 13);
}

MICRONOTES_TEST(markdown_parser_covers_syntax_reference_blocks) {
  const std::filesystem::path syntaxFixture = std::filesystem::path(__FILE__).parent_path().parent_path() / "docs/markdown-elements.md";
  std::ifstream in {syntaxFixture};
  MICRONOTES_REQUIRE(static_cast<bool>(in));
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const auto doc = micronotes::markdown::MarkdownParser().parse(buffer.str());
  int headings = 0;
  int orderedItems = 0;
  int unorderedItems = 0;
  int quotes = 0;
  int codeBlocks = 0;
  int links = 0;
  for(const auto& block : doc.blocks) {
    if(block.type == micronotes::markdown::BlockType::Heading) ++headings;
    if(block.type == micronotes::markdown::BlockType::OrderedItem) ++orderedItems;
    if(block.type == micronotes::markdown::BlockType::UnorderedItem) ++unorderedItems;
    if(block.type == micronotes::markdown::BlockType::Quote) ++quotes;
    if(block.type == micronotes::markdown::BlockType::Code) ++codeBlocks;
    for(const auto& inlineItem : block.inlines) {
      if(inlineItem.type == micronotes::markdown::InlineType::Link) ++links;
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& inlineItem : cell.inlines) {
          if(inlineItem.type == micronotes::markdown::InlineType::Link) ++links;
        }
      }
    }
  }
  MICRONOTES_REQUIRE(headings >= 8);
  MICRONOTES_REQUIRE(orderedItems >= 3);
  MICRONOTES_REQUIRE(unorderedItems >= 3);
  MICRONOTES_REQUIRE(quotes >= 2);
  MICRONOTES_REQUIRE(codeBlocks >= 2);
  MICRONOTES_REQUIRE(links >= 8);
}
