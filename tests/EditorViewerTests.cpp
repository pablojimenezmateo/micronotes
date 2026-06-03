#include "TestSupport.h"

#include "editor/MarkdownEditor.h"
#include "markdown/MarkdownParser.h"
#include "ui/ShellModel.h"
#include "viewer/MarkdownViewer.h"

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

MICRONOTES_TEST(viewer_layout_counts_blocks) {
  auto doc = micronotes::markdown::MarkdownParser().parse("# One\nText\n");
  auto layout = micronotes::viewer::MarkdownViewer().layout(doc, 800);
  MICRONOTES_REQUIRE(layout.width == 800);
  MICRONOTES_REQUIRE(layout.blockCount == 2);
  MICRONOTES_REQUIRE(layout.totalHeight > 0);
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

MICRONOTES_TEST(pane_controller_tracks_visibility_modes) {
  micronotes::ui::PaneController panes;
  panes.setMode(micronotes::ui::PaneMode::Editor);
  MICRONOTES_REQUIRE(panes.editorVisible());
  MICRONOTES_REQUIRE(!panes.viewerVisible());
  panes.setMode(micronotes::ui::PaneMode::Viewer);
  MICRONOTES_REQUIRE(!panes.editorVisible());
  MICRONOTES_REQUIRE(panes.viewerVisible());
  panes.setMode(micronotes::ui::PaneMode::Split);
  MICRONOTES_REQUIRE(panes.editorVisible());
  MICRONOTES_REQUIRE(panes.viewerVisible());
}

MICRONOTES_TEST(debounced_refresh_waits_before_refreshing) {
  micronotes::ui::DebouncedRefresh refresh(100);
  refresh.markDirty(1000);
  MICRONOTES_REQUIRE(!refresh.shouldRefresh(1050));
  MICRONOTES_REQUIRE(refresh.shouldRefresh(1100));
  refresh.markRefreshed();
  MICRONOTES_REQUIRE(!refresh.shouldRefresh(1200));
}
