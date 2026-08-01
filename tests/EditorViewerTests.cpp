#include "CoreAliases.h"
#include "TestSupport.h"

#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/markdown/MarkdownParser.h"
#include "core/ui/ShellModel.h"
#include "core/viewer/MarkdownViewer.h"

#include <filesystem>
#include <fstream>
#include <sstream>

MICRONOTES_TEST(editor_tracks_dirty_state) {
  microcore::editor::MarkdownEditor editor;
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
  microcore::editor::MarkdownEditor editor;
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
  microcore::editor::MarkdownEditor editor;
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
  microcore::editor::MarkdownEditor editor;
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
    editor.breakUndoGroup();  // one deliberate edit each, not one typing run
    editor.insert("x");
  }
  int undoCount = 0;
  while(editor.undo()) {
    ++undoCount;
  }
  MICRONOTES_REQUIRE(undoCount == 100);
  MICRONOTES_REQUIRE(editor.text().size() == 5);
}

MICRONOTES_TEST(editor_coalesces_a_typing_run_into_one_undo_step) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("");
  for(const char* c : {"h", "e", "l", "l", "o"}) editor.insert(c);
  MICRONOTES_REQUIRE(editor.text() == "hello");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text().empty());
  MICRONOTES_REQUIRE(!editor.undo());

  // Backspacing is its own run, and a newline is its own step.
  editor.setText("ab");
  editor.erasePrevious();
  editor.erasePrevious();
  MICRONOTES_REQUIRE(editor.text().empty());
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "ab");

  editor.setText("");
  editor.insert("one");
  editor.insert("\n");
  editor.insert("two");
  MICRONOTES_REQUIRE(editor.text() == "one\ntwo");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "one\n");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "one");
}

MICRONOTES_TEST(editor_breaks_the_typing_run_when_the_caret_moves) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("");
  editor.insert("ab");
  editor.moveCursor(0);
  editor.insert("X");
  MICRONOTES_REQUIRE(editor.text() == "Xab");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "ab");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text().empty());
}

MICRONOTES_TEST(editor_replaces_a_range_and_restores_it_on_undo) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("hello world");
  editor.replaceRange(6, 11, "there");
  MICRONOTES_REQUIRE(editor.text() == "hello there");
  MICRONOTES_REQUIRE(editor.cursor() == 11);
  editor.replaceRange(0, 0, "> ");
  MICRONOTES_REQUIRE(editor.text() == "> hello there");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "hello there");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "hello world");
}

MICRONOTES_TEST(editor_moves_and_erases_by_words) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha beta-gamma delta");
  editor.moveCursor(editor.text().size());
  editor.moveWordLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 17);  // start of "delta"
  editor.moveWordLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 11);  // start of "gamma"
  editor.moveWordLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 10);  // the hyphen is its own run
  editor.moveWordRight();
  MICRONOTES_REQUIRE(editor.cursor() == 11);  // back over the hyphen
  editor.moveWordRight();
  MICRONOTES_REQUIRE(editor.cursor() == 16);  // end of "gamma"

  editor.moveCursor(editor.text().size());
  editor.erasePreviousWord();
  MICRONOTES_REQUIRE(editor.text() == "alpha beta-gamma ");
  editor.moveCursor(0);
  editor.eraseNextWord();
  MICRONOTES_REQUIRE(editor.text() == " beta-gamma ");
}

MICRONOTES_TEST(editor_extends_a_selection_with_shifted_movement) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha beta\ngamma");
  editor.moveCursor(0);
  editor.moveWordRight(true);
  MICRONOTES_REQUIRE(editor.hasSelection());
  MICRONOTES_REQUIRE(editor.selectedText() == "alpha");
  editor.moveLineEnd(true);
  MICRONOTES_REQUIRE(editor.selectedText() == "alpha beta");
  editor.moveLineDown(true);
  MICRONOTES_REQUIRE(editor.selectedText() == "alpha beta\ngamma");
  editor.moveDocumentStart(true);
  MICRONOTES_REQUIRE(!editor.hasSelection());

  // An unshifted arrow collapses the selection instead of stepping past it.
  editor.selectRange(2, 6);
  editor.moveLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 2);
  MICRONOTES_REQUIRE(!editor.hasSelection());
  editor.selectRange(2, 6);
  editor.moveRight();
  MICRONOTES_REQUIRE(editor.cursor() == 6);
}

MICRONOTES_TEST(editor_jumps_to_document_bounds) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("one\ntwo\nthree");
  editor.moveCursor(5);
  editor.moveDocumentStart();
  MICRONOTES_REQUIRE(editor.cursor() == 0);
  editor.moveDocumentEnd();
  MICRONOTES_REQUIRE(editor.cursor() == editor.text().size());
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

MICRONOTES_TEST(viewer_layout_counts_blocks) {
  auto doc = microcore::markdown::MarkdownParser().parse("# One\nText\n");
  auto layout = microcore::viewer::MarkdownViewer().layout(doc, 800);
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
  const auto doc = microcore::markdown::MarkdownParser().parse(buffer.str());
  int headings = 0;
  int orderedItems = 0;
  int unorderedItems = 0;
  int quotes = 0;
  int codeBlocks = 0;
  int links = 0;
  for(const auto& block : doc.blocks) {
    if(block.type == microcore::markdown::BlockType::Heading) ++headings;
    if(block.type == microcore::markdown::BlockType::OrderedItem) ++orderedItems;
    if(block.type == microcore::markdown::BlockType::UnorderedItem) ++unorderedItems;
    if(block.type == microcore::markdown::BlockType::Quote) ++quotes;
    if(block.type == microcore::markdown::BlockType::Code) ++codeBlocks;
    for(const auto& inlineItem : block.inlines) {
      if(inlineItem.type == microcore::markdown::InlineType::Link) ++links;
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& inlineItem : cell.inlines) {
          if(inlineItem.type == microcore::markdown::InlineType::Link) ++links;
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

MICRONOTES_TEST(editor_anchors_a_new_selection_at_the_caret_after_typing) {
  micronotes::editor::MarkdownEditor editor;
  editor.setText("alpha ");
  editor.moveCursor(editor.text().size());
  editor.insert("beta");
  // Typing leaves no selection, so extending starts from where the caret is,
  // not from wherever the anchor was last parked.
  MICRONOTES_REQUIRE(!editor.hasSelection());
  editor.moveWordLeft(true);
  MICRONOTES_REQUIRE(editor.selectedText() == "beta");
}

MICRONOTES_TEST(pane_controller_tracks_visibility_modes) {
  microcore::ui::PaneController panes;
  panes.setMode(microcore::ui::PaneMode::Editor);
  MICRONOTES_REQUIRE(panes.editorVisible());
  MICRONOTES_REQUIRE(!panes.viewerVisible());
  panes.setMode(microcore::ui::PaneMode::Viewer);
  MICRONOTES_REQUIRE(!panes.editorVisible());
  MICRONOTES_REQUIRE(panes.viewerVisible());
  panes.setMode(microcore::ui::PaneMode::Split);
  MICRONOTES_REQUIRE(panes.editorVisible());
  MICRONOTES_REQUIRE(panes.viewerVisible());
}

MICRONOTES_TEST(debounced_refresh_waits_before_refreshing) {
  microcore::ui::DebouncedRefresh refresh(100);
  refresh.markDirty(1000);
  MICRONOTES_REQUIRE(!refresh.shouldRefresh(1050));
  MICRONOTES_REQUIRE(refresh.shouldRefresh(1100));
  refresh.markRefreshed();
  MICRONOTES_REQUIRE(!refresh.shouldRefresh(1200));
}

// --- editor: UTF-8 caret arithmetic ----------------------------------------
//
// The caret is a byte offset, which is the right representation, but it used to
// *move* a byte at a time. Stepping into or deleting across a multi-byte code
// point split it and left an invalid sequence behind.

MICRONOTES_TEST(editor_backspace_removes_a_whole_code_point) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("caf\xc3\xa9");  // "café" -- the e-acute is two bytes
  MICRONOTES_REQUIRE(editor.cursor() == 5);
  editor.erasePrevious();
  MICRONOTES_REQUIRE(editor.text() == "caf");
  MICRONOTES_REQUIRE(editor.cursor() == 3);
}

MICRONOTES_TEST(editor_arrow_steps_over_a_whole_code_point) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("caf\xc3\xa9x");
  editor.moveDocumentStart();
  for(int i = 0; i < 3; ++i) editor.moveRight();
  MICRONOTES_REQUIRE(editor.cursor() == 3);
  editor.moveRight();
  MICRONOTES_REQUIRE(editor.cursor() == 5);  // skipped both bytes, not one
  editor.moveLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 3);
}

MICRONOTES_TEST(editor_vertical_motion_uses_code_point_columns) {
  microcore::editor::MarkdownEditor editor;
  // Column 3 on the first line is past the two-byte character; a byte-based
  // column would land at a different character on the line below.
  editor.setText("\xc3\xa9\xc3\xa9xy\nabcde");
  editor.moveDocumentStart();
  for(int i = 0; i < 3; ++i) editor.moveRight();
  editor.moveLineDown();
  const auto lineStart = editor.text().find('\n') + 1;
  MICRONOTES_REQUIRE(editor.cursor() == lineStart + 3);
}

// --- editor: keyboard selection --------------------------------------------
//
// Every motion collapsed the selection unconditionally, so Shift+Arrow could
// not select at all -- only Home and End honoured the modifier.

MICRONOTES_TEST(editor_shift_arrow_extends_the_selection) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("hello world");
  editor.moveDocumentStart();
  for(int i = 0; i < 5; ++i) editor.moveRight(true);
  MICRONOTES_REQUIRE(editor.hasSelection());
  MICRONOTES_REQUIRE(editor.selectedText() == "hello");
  editor.moveLeft(true);
  MICRONOTES_REQUIRE(editor.selectedText() == "hell");
}

MICRONOTES_TEST(editor_unshifted_arrow_collapses_to_the_selection_edge) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("hello world");
  editor.selectRange(2, 7);
  editor.moveLeft();
  MICRONOTES_REQUIRE(!editor.hasSelection());
  MICRONOTES_REQUIRE(editor.cursor() == 2);
  editor.selectRange(2, 7);
  editor.moveRight();
  MICRONOTES_REQUIRE(editor.cursor() == 7);
}

MICRONOTES_TEST(editor_shift_vertical_motion_extends_the_selection) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("one\ntwo\nthree");
  editor.moveDocumentStart();
  editor.moveLineDown(true);
  MICRONOTES_REQUIRE(editor.hasSelection());
  MICRONOTES_REQUIRE(editor.selectedText() == "one\n");
}

// --- editor: word motion ----------------------------------------------------

MICRONOTES_TEST(editor_word_motion_crosses_words_not_characters) {
  microcore::editor::MarkdownEditor editor;
  // Word motion stops at run boundaries: rightwards at the end of the run it
  // crossed, leftwards at the start of it.
  editor.setText("alpha beta gamma");
  editor.moveDocumentStart();
  editor.moveWordRight();
  MICRONOTES_REQUIRE(editor.cursor() == 5);
  editor.moveWordRight();
  MICRONOTES_REQUIRE(editor.cursor() == 10);
  editor.moveWordLeft();
  MICRONOTES_REQUIRE(editor.cursor() == 6);
}

MICRONOTES_TEST(editor_word_delete_removes_the_word_before_the_caret) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("alpha beta");
  editor.erasePreviousWord();
  MICRONOTES_REQUIRE(editor.text() == "alpha ");
  editor.erasePreviousWord();
  MICRONOTES_REQUIRE(editor.text() == "");
}

// --- editor: undo -----------------------------------------------------------
//
// Undo used to push a full document copy per keystroke, unbounded, and restore
// no caret. Ctrl+Z therefore rewound one character at a time and left the caret
// wherever it happened to be.

MICRONOTES_TEST(editor_undo_coalesces_a_run_of_typing) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("");
  for(const char c : std::string("hello")) editor.insert(std::string(1, c));
  MICRONOTES_REQUIRE(editor.text() == "hello");
  // One record for the whole run, not five.
  MICRONOTES_REQUIRE(editor.undoDepth() == 1);
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "");
}

MICRONOTES_TEST(editor_undo_breaks_the_run_at_a_newline) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("");
  for(const char c : std::string("ab")) editor.insert(std::string(1, c));
  editor.insert("\n");
  for(const char c : std::string("cd")) editor.insert(std::string(1, c));
  MICRONOTES_REQUIRE(editor.text() == "ab\ncd");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "ab\n");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "ab");
}

MICRONOTES_TEST(editor_undo_restores_the_caret_it_was_taken_with) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("hello world");
  editor.moveCursor(5);
  editor.insert(",");
  MICRONOTES_REQUIRE(editor.text() == "hello, world");
  editor.moveDocumentEnd();
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "hello world");
  // Back where the edit happened, not at the end where the caret had wandered.
  MICRONOTES_REQUIRE(editor.cursor() == 5);
}

MICRONOTES_TEST(editor_undo_history_is_bounded) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(4096, 'x'));
  // Each caret jump seals the previous record, so this is 2000 distinct edits.
  for(int i = 0; i < 2000; ++i) {
    editor.moveCursor(static_cast<std::size_t>(i % 100));
    editor.insert("a");
  }
  MICRONOTES_REQUIRE(editor.undoDepth() <= 256);
  MICRONOTES_REQUIRE(editor.undoBytes() <= 32u * 1024u * 1024u);
}

MICRONOTES_TEST(editor_redo_returns_the_undone_edit) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("a");
  editor.moveDocumentEnd();
  editor.insert("b");
  MICRONOTES_REQUIRE(editor.undo());
  MICRONOTES_REQUIRE(editor.text() == "a");
  MICRONOTES_REQUIRE(editor.redo());
  MICRONOTES_REQUIRE(editor.text() == "ab");
}
