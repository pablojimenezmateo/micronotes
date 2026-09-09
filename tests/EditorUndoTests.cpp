#include "CoreAliases.h"
#include "TestSupport.h"
#include "EditorFixture.h"

#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"

#include <cstdint>

using micronotes::tests::wordsFromScratch;

// The editor's undo history: what one step is, how a run of typing folds into
// one, and the bounds that keep a long session from growing without limit.

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

// The test above drove a 4 KB buffer, so it measured 400 KB of history against
// a 32 MB assertion and could not have failed. A snapshot was the whole buffer,
// so what bounded the memory was the note's size times the depth -- and on a
// note the size the rest of the harness uses, the count ceiling alone left
// 19.6 MB of undo for one open note, against a whole-process peak RSS of about
// 30 MB.
//
// Driven at 200 KB. A step is a splice now, so what this pins is the thing the
// byte ceiling could not: retention that does not scale with the note at all.
MICRONOTES_TEST(editor_undo_history_is_bounded_in_bytes_on_a_large_note) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(200 * 1024, 'x'));
  for(int i = 0; i < 400; ++i) {
    editor.moveCursor(static_cast<std::size_t>(i % 100));
    editor.insert("a");
  }
  // 400 insertions of one byte retain 400 bytes of text plus a step apiece. A
  // snapshot history retained 8 MB here and had dropped to 40 steps to do it.
  MICRONOTES_REQUIRE(editor.undoBytes() <= 64u * 1024u);
  // And the note's size is now not an input: the same session on a 2 KB note
  // must retain the same order of bytes, which is the ratio that was 107x.
  MICRONOTES_REQUIRE(editor.undoDepth() == 100);
  for(int i = 0; i < 100; ++i) MICRONOTES_REQUIRE(editor.undo());
}

// The claim above, stated as the comparison rather than as two numbers: a long
// session retains what it *edited*, so the note's size drops out.
MICRONOTES_TEST(editor_undo_retention_does_not_scale_with_the_note) {
  const auto retained = [](std::size_t noteBytes) {
    microcore::editor::MarkdownEditor editor;
    editor.setText(std::string(noteBytes, 'x'));
    for(int i = 0; i < 200; ++i) {
      editor.moveCursor(static_cast<std::size_t>(i) % (noteBytes + 1));
      editor.insert("a");
    }
    return editor.undoBytes();
  };
  const std::size_t small = retained(2u * 1024u);
  const std::size_t large = retained(200u * 1024u);
  // A hundred times the note, the same history. Under snapshots this was
  // 214.6 KB against 8 MB.
  MICRONOTES_REQUIRE(small == large);
  MICRONOTES_REQUIRE(small <= 32u * 1024u);
}

// Deleting text is the one thing a history has to keep bytes for: what came out
// is the only copy left. So this is the case the byte ceiling now exists for,
// and it must stay bounded without emptying the history it is trimming.
MICRONOTES_TEST(editor_undo_bounds_a_history_of_whole_note_deletions) {
  microcore::editor::MarkdownEditor editor;
  const std::size_t noteBytes = 200u * 1024u;
  editor.setText(std::string(noteBytes, 'x'));
  for(int i = 0; i < 80; ++i) {
    editor.selectAll();
    editor.insert(std::string(noteBytes, static_cast<char>('a' + (i % 26))));
  }
  MICRONOTES_REQUIRE(editor.undoBytes() <= 9u * 1024u * 1024u);
  MICRONOTES_REQUIRE(editor.undoDepth() >= 8);
  // And every step it kept still undoes to the right bytes.
  for(int i = 0; i < 8; ++i) {
    MICRONOTES_REQUIRE(editor.undo());
    MICRONOTES_REQUIRE(editor.text().size() == noteBytes);
  }
}

// A note one step of which already exceeds the whole budget still has undo:
// the floor is what stops the trim from emptying the history it is trimming.
MICRONOTES_TEST(editor_undo_survives_a_note_larger_than_the_byte_budget) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(12u * 1024u * 1024u, 'x'));
  for(int i = 0; i < 12; ++i) {
    editor.moveCursor(static_cast<std::size_t>(i));
    editor.insert("a");
  }
  MICRONOTES_REQUIRE(editor.undoDepth() >= 1);
  MICRONOTES_REQUIRE(editor.undo());
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

// A snapshot history round-tripped trivially: undo restored a copy of a buffer
// that had actually existed. A splice history *reconstructs* the earlier buffer
// from an offset and a string, so "Ctrl+Z gives back exactly what was there" is
// real logic and these two tests are what hold it.
//
// The first drives fewer edits than the depth ceiling and seals every one into
// its own step, so the exact state before each edit is known and every stop can
// be compared against it. That is the strong claim -- undo reconstructs the
// bytes -- and it needs a history nothing has trimmed to make it.
MICRONOTES_TEST(editor_undo_reconstructs_every_state_it_passed_through) {
  const char* fragments[] = {"a", " ", "\n", "word", " two words ", ".", "xyz", "\n\n", "  q"};
  for(const std::uint64_t seed : {0xA5A5A5A5A5A5A5A5ull, 0x0123456789ABCDEFull, 0xFEDCBA9876543210ull}) {
    std::uint64_t state = seed;
    const auto next = [&](std::uint64_t bound) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      return bound == 0 ? 0ull : state % bound;
    };

    microcore::editor::MarkdownEditor editor;
    editor.setText("the quick brown fox");
    std::vector<std::string> history;

    // 80 edits, under the 100-step ceiling, so nothing is dropped.
    for(int step = 0; step < 80; ++step) {
      const std::size_t size = editor.text().size();
      const std::string before = editor.text();
      const std::size_t depthBefore = editor.undoDepth();
      // Sealed, so each edit is its own stop and `history` can be exact.
      editor.breakUndoGroup();
      switch(next(5)) {
        case 0: editor.moveTo(next(size + 1), false);
                editor.insert(fragments[next(std::size(fragments))]); break;
        case 1: editor.moveTo(next(size + 1), false); editor.erasePrevious(); break;
        case 2: editor.moveTo(next(size + 1), false); editor.eraseNext(); break;
        case 3: {
          const std::size_t from = next(size + 1);
          editor.selectRange(from, from + next(size - from + 1));
          editor.insert(fragments[next(std::size(fragments))]);
          break;
        }
        default: {
          const std::size_t from = next(size + 1);
          editor.replaceRange(from, from + next(size - from + 1),
                              fragments[next(std::size(fragments))]);
          break;
        }
      }
      // A rejected no-op edit is not a stop, and neither is it a change.
      if(editor.undoDepth() == depthBefore) {
        MICRONOTES_REQUIRE(editor.text() == before);
        continue;
      }
      MICRONOTES_REQUIRE(editor.undoDepth() == depthBefore + 1);
      history.push_back(before);
    }

    MICRONOTES_REQUIRE(editor.undoDepth() == history.size());
    // All the way back, checking the reconstruction at every stop.
    for(std::size_t i = history.size(); i > 0; --i) {
      MICRONOTES_REQUIRE(editor.undo());
      MICRONOTES_REQUIRE(editor.text() == history[i - 1]);
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
      MICRONOTES_REQUIRE(editor.cursor() <= editor.text().size());
      MICRONOTES_REQUIRE(editor.selectionEnd() <= editor.text().size());
    }
    MICRONOTES_REQUIRE(!editor.undo());
    // Back to where `setText` left it, byte for byte.
    MICRONOTES_REQUIRE(editor.text() == "the quick brown fox");

    // And forwards again through the same states.
    for(std::size_t i = 0; i + 1 < history.size(); ++i) {
      MICRONOTES_REQUIRE(editor.redo());
      MICRONOTES_REQUIRE(editor.text() == history[i + 1]);
    }
  }
}

// The second drives far past the ceiling and lets runs coalesce, so where the
// stops are is the editor's business rather than something the test predicts.
// What it holds is the property that does not depend on that: undo and redo are
// exact inverses, so walking the whole history back and then forwards again
// lands on the same buffers in the same order.
MICRONOTES_TEST(editor_undo_and_redo_are_exact_inverses) {
  const char* fragments[] = {"a", " ", "\n", "word", " two words ", "", ".", "xyz", "\n\n"};
  for(const std::uint64_t seed : {0x9E3779B97F4A7C15ull, 0xC0FFEE0DDF00D123ull}) {
    std::uint64_t state = seed;
    const auto next = [&](std::uint64_t bound) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      return bound == 0 ? 0ull : state % bound;
    };

    microcore::editor::MarkdownEditor editor;
    editor.setText("the quick brown fox jumps over the lazy dog");

    for(int step = 0; step < 400; ++step) {
      const std::size_t size = editor.text().size();
      switch(next(6)) {
        // Typing on with no caret move, which is what folds into the open step.
        case 0: editor.insert(fragments[next(std::size(fragments))]); break;
        case 1: editor.moveTo(next(size + 1), false);
                editor.insert(fragments[next(std::size(fragments))]); break;
        // Runs of two, so the second key exercises the fold and not just the
        // opening of a step. Backspace folds leftwards, Delete rightwards.
        case 2: editor.moveTo(next(size + 1), false);
                editor.erasePrevious(); editor.erasePrevious(); break;
        case 3: editor.moveTo(next(size + 1), false);
                editor.eraseNext(); editor.eraseNext(); break;
        case 4: {
          const std::size_t from = next(size + 1);
          editor.selectRange(from, from + next(size - from + 1));
          editor.insert(fragments[next(std::size(fragments))]);
          break;
        }
        default: {
          const std::size_t from = next(size + 1);
          editor.replaceRange(from, from + next(size - from + 1),
                              fragments[next(std::size(fragments))]);
          break;
        }
      }
    }

    // Every buffer the way back, including the one it started from.
    std::vector<std::string> backwards {editor.text()};
    while(editor.undo()) {
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
      MICRONOTES_REQUIRE(editor.cursor() <= editor.text().size());
      backwards.push_back(editor.text());
    }
    // The ceiling means it does not reach the original, but it must reach a
    // real stop and stop cleanly there.
    MICRONOTES_REQUIRE(backwards.size() > 1);

    // Forwards again: the same buffers, in reverse order, exactly.
    for(std::size_t i = backwards.size() - 1; i > 0; --i) {
      MICRONOTES_REQUIRE(editor.redo());
      MICRONOTES_REQUIRE(editor.wordCount() == wordsFromScratch(editor.text()));
      MICRONOTES_REQUIRE(editor.text() == backwards[i - 1]);
    }
    MICRONOTES_REQUIRE(!editor.redo());
  }
}

// The three directions a run of edits folds into one step, named rather than
// left to the random walk. A snapshot history needed none of this: folding meant
// declining to push. A splice has to be widened, and the shape of the widening
// differs per direction -- which is three chances to put bytes back in the
// wrong order or at the wrong offset.
MICRONOTES_TEST(editor_undo_folds_a_run_in_each_direction) {
  // Typing extends the run's tail.
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText("start");
    editor.moveDocumentEnd();
    for(const char c : std::string(" and more")) editor.insert(std::string(1, c));
    MICRONOTES_REQUIRE(editor.text() == "start and more");
    MICRONOTES_REQUIRE(editor.undoDepth() == 1);
    MICRONOTES_REQUIRE(editor.undo());
    MICRONOTES_REQUIRE(editor.text() == "start");
  }
  // Backspace walks left: each key takes the bytes before the ones already
  // taken, so the step's start moves back and the bytes go on the *front* of
  // what it has to restore. Reversed, this spells "cdef" instead of "fedc".
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText("abcdef");
    editor.moveDocumentEnd();
    for(int i = 0; i < 4; ++i) editor.erasePrevious();
    MICRONOTES_REQUIRE(editor.text() == "ab");
    MICRONOTES_REQUIRE(editor.undoDepth() == 1);
    MICRONOTES_REQUIRE(editor.undo());
    MICRONOTES_REQUIRE(editor.text() == "abcdef");
  }
  // Delete eats forwards from a caret that stands still, so those bytes go on
  // the *end* of the same string. Reversed, this spells "fedc".
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText("abcdef");
    editor.moveCursor(2);
    for(int i = 0; i < 4; ++i) editor.eraseNext();
    MICRONOTES_REQUIRE(editor.text() == "ab");
    MICRONOTES_REQUIRE(editor.undoDepth() == 1);
    MICRONOTES_REQUIRE(editor.undo());
    MICRONOTES_REQUIRE(editor.text() == "abcdef");
    MICRONOTES_REQUIRE(editor.cursor() == 2);
  }
  // Multi-byte, in both erase directions: a fold that splits a codepoint is a
  // fold that puts mojibake back.
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText("a\xC3\xA9\xC3\xBCz");  // "aéüz"
    editor.moveDocumentEnd();
    editor.erasePrevious();
    editor.erasePrevious();
    editor.erasePrevious();
    MICRONOTES_REQUIRE(editor.text() == "a");
    MICRONOTES_REQUIRE(editor.undo());
    MICRONOTES_REQUIRE(editor.text() == "a\xC3\xA9\xC3\xBCz");
  }
}

// Undo is an edit like any other now, so it reports the span it changed rather
// than the whole buffer. That is what lets the live layout bound its work on a
// Ctrl+Z instead of re-comparing the note.
MICRONOTES_TEST(editor_undo_reports_the_span_it_changed) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(4096, 'x'));
  editor.moveCursor(2048);
  editor.insert("hello");

  editor.undo();
  const auto& change = editor.lastChange();
  // Five bytes went out at 2048, and nothing else moved. Under snapshots this
  // reported [0, 4101) -> [0, 4096): the whole note, which bounds nothing.
  MICRONOTES_REQUIRE(change.start == 2048);
  MICRONOTES_REQUIRE(change.oldEnd == 2053);
  MICRONOTES_REQUIRE(change.newEnd == 2048);
  MICRONOTES_REQUIRE(editor.text().size() == 4096);

  editor.redo();
  const auto& forward = editor.lastChange();
  MICRONOTES_REQUIRE(forward.start == 2048);
  MICRONOTES_REQUIRE(forward.oldEnd == 2048);
  MICRONOTES_REQUIRE(forward.newEnd == 2053);
}
