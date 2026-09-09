#include "CoreAliases.h"
#include "TestSupport.h"
#include "EditorFixture.h"

#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"

#include <cstdint>

using micronotes::tests::wordsFromScratch;

// The buffer itself: the caret, the selection, and the counts kept alongside
// them. Undo lives in `EditorUndoTests.cpp` and wrapping in
// `EditorSoftWrapTests.cpp`; between them those were two thirds of this file.

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

// Consumers derive per-frame work from the buffer, and a frame happens for
// reasons that have nothing to do with the text. Without this they either
// recompute every frame or compare the whole string; the status bar was doing
// the first, walking a 235 KB note byte by byte on every scroll frame.
MICRONOTES_TEST(editor_revision_moves_only_when_the_text_does) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("hello");
  const std::uint64_t opened = editor.revision();

  // Moving around is not a change.
  editor.moveCursor(0);
  editor.selectAll();
  editor.clearSelection();
  MICRONOTES_REQUIRE(editor.revision() == opened);

  editor.moveCursor(editor.text().size());
  editor.insert(" there");
  const std::uint64_t typed = editor.revision();
  MICRONOTES_REQUIRE(typed != opened);

  editor.erasePrevious();
  MICRONOTES_REQUIRE(editor.revision() != typed);

  // Undo restores earlier bytes but issues a new revision. That direction is
  // the safe one: a consumer recomputes something it need not have, rather than
  // showing an answer for a buffer that is no longer on screen.
  const std::uint64_t erased = editor.revision();
  editor.undo();
  MICRONOTES_REQUIRE(editor.revision() != erased);

  // Loading another note is a change even though nothing was edited.
  const std::uint64_t before = editor.revision();
  editor.setText("a different note");
  MICRONOTES_REQUIRE(editor.revision() != before);
}

// The word count is carried across every edit rather than recomputed, because
// recomputing it walks the whole note on every keystroke. The risk that buys is
// the nasty kind: a count that drifts is wrong quietly, in a corner of the
// status bar, with nothing to point at.
//
// So it is checked against a from-scratch count after every single edit of a
// long random sequence, over text chosen to hit the cases the increment reasons
// about: an edit landing inside a word, on the space between two, at either end
// of the buffer, and one that joins or splits words.
MICRONOTES_TEST(editor_word_count_matches_a_full_recount_after_every_edit) {
  const char* fragments[] = {"a", " ", "  ", "\n", "word", " word ", "x y", "\n\n", "", ".", "ab cd ef"};
  for(const std::uint64_t seed : {0x9E3779B97F4A7C15ull, 0x1234567891234567ull, 0xDEADBEEFCAFEF00Dull}) {
    std::uint64_t state = seed;
    const auto next = [&](std::uint64_t bound) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      return bound == 0 ? 0ull : state % bound;
    };
    microcore::editor::MarkdownEditor editor;
    editor.setText("the quick brown fox jumps over the lazy dog");
    MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));

    for(int step = 0; step < 400; ++step) {
      const std::size_t size = editor.text().size();
      switch(next(4)) {
        case 0: {
          editor.moveTo(next(size + 1), false);
          editor.insert(fragments[next(std::size(fragments))]);
          break;
        }
        case 1: {
          editor.moveTo(next(size + 1), false);
          editor.erasePrevious();
          break;
        }
        case 2: {
          editor.moveTo(next(size + 1), false);
          editor.eraseNext();
          break;
        }
        default: {
          const std::size_t from = next(size + 1);
          const std::size_t to = from + next(size - from + 1);
          editor.replaceRange(from, to, fragments[next(std::size(fragments))]);
          break;
        }
      }
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
    }

    // And across undo and redo, which carry the count across a splice like any
    // other edit rather than recounting the buffer -- so this walk checks the
    // incremental path in both directions, not a recount standing in for it.
    while(editor.undo()) {
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
    }
    while(editor.redo()) {
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
    }
  }
}

// The edge cases the window arithmetic turns on, named rather than left to the
// random walk to stumble into.
MICRONOTES_TEST(editor_word_count_handles_the_edges_of_the_buffer) {
  microcore::editor::MarkdownEditor editor;
  editor.setText("");
  MICRONOTES_REQUIRE(editor.wordCount() == 0);

  // A word appearing, then split in two by a space typed into its middle, then
  // joined again by deleting that space.
  editor.insert("ab");
  MICRONOTES_REQUIRE(editor.wordCount() == 1);
  editor.moveTo(1, false);
  editor.insert(" ");
  MICRONOTES_REQUIRE(editor.text() == "a b");
  MICRONOTES_REQUIRE(editor.wordCount() == 2);
  editor.moveTo(2, false);
  editor.erasePrevious();
  MICRONOTES_REQUIRE(editor.text() == "ab");
  MICRONOTES_REQUIRE(editor.wordCount() == 1);

  // At the very end of the buffer, which is the position the window clamps at.
  editor.moveDocumentEnd();
  editor.insert(" c");
  MICRONOTES_REQUIRE(editor.wordCount() == 2);
  // And at the very start, which is the one position with no predecessor byte.
  editor.moveTo(0, false);
  editor.insert("z ");
  MICRONOTES_REQUIRE(editor.text() == "z ab c");
  MICRONOTES_REQUIRE(editor.wordCount() == 3);

  // A buffer with no whitespace at all: one word however long it is, and the
  // window must not walk it looking for a boundary.
  editor.setText(std::string(4096, 'x'));
  MICRONOTES_REQUIRE(editor.wordCount() == 1);
  editor.moveTo(2048, false);
  editor.insert("y");
  MICRONOTES_REQUIRE(editor.wordCount() == 1);
}
