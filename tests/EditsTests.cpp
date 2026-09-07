#include "CoreAliases.h"
#include "TestSupport.h"

#include "doc/Edits.h"
#include "core/editor/MarkdownEditor.h"

#include <algorithm>
#include <string>

namespace {

using micronotes::doc::BlockKind;
using micronotes::doc::Edit;

std::string applied(const std::string& source, const Edit& edit) {
  MICRONOTES_REQUIRE(edit.valid);
  std::string out = source;
  out.erase(edit.start, edit.end - edit.start);
  out.insert(edit.start, edit.text);
  return out;
}

}

MICRONOTES_TEST(edits_turn_a_paragraph_into_other_blocks) {
  const std::string source = "hello\n";
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Heading, 2)) == "## hello\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Bullet)) == "- hello\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Todo)) == "- [ ] hello\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Ordered)) == "1. hello\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Quote)) == "> hello\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Divider)) == "---\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::turnInto(source, 2, BlockKind::Code)) == "```\nhello\n```\n");

  // The caret keeps pointing at the same character as the marker grows.
  const auto heading = micronotes::doc::turnInto(source, 2, BlockKind::Heading, 2);
  MICRONOTES_REQUIRE(heading.cursor == 5);
}

MICRONOTES_TEST(edits_turn_a_heading_back_into_text) {
  const std::string source = "## hello\n";
  const auto edit = micronotes::doc::turnInto(source, 5, BlockKind::Paragraph);
  MICRONOTES_REQUIRE(applied(source, edit) == "hello\n");
  MICRONOTES_REQUIRE(edit.cursor == 2);
  // Asking for the shape it already has changes nothing.
  MICRONOTES_REQUIRE(!micronotes::doc::turnInto(source, 5, BlockKind::Heading, 2).valid);
  MICRONOTES_REQUIRE(micronotes::doc::turnInto(source, 5, BlockKind::Heading, 3).valid);
}

MICRONOTES_TEST(edits_refuse_to_rewrite_blocks_the_scanner_does_not_model) {
  const std::string source = "| a | b |\n|---|---|\n| 1 | 2 |\n";
  MICRONOTES_REQUIRE(!micronotes::doc::turnInto(source, 4, BlockKind::Bullet).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::outdentOrUnwrap(source, 0).valid);
}

MICRONOTES_TEST(edits_toggle_a_task_checkbox) {
  const std::string unchecked = "- [ ] task\n";
  const auto tick = micronotes::doc::toggleTodo(unchecked, 6);
  MICRONOTES_REQUIRE(applied(unchecked, tick) == "- [x] task\n");
  MICRONOTES_REQUIRE(tick.cursor == 6);

  const std::string checked = "- [x] task\n";
  MICRONOTES_REQUIRE(applied(checked, micronotes::doc::toggleTodo(checked, 6)) == "- [ ] task\n");

  // A bullet gains a checkbox; a paragraph gains the whole marker.
  const std::string bullet = "- task\n";
  MICRONOTES_REQUIRE(applied(bullet, micronotes::doc::toggleTodo(bullet, 4)) == "- [ ] task\n");
  const std::string plain = "task\n";
  MICRONOTES_REQUIRE(applied(plain, micronotes::doc::toggleTodo(plain, 2)) == "- [ ] task\n");
}

MICRONOTES_TEST(edits_indent_and_outdent_list_items) {
  const std::string source = "- one\n- two\n";
  const auto in = micronotes::doc::indent(source, 9);
  MICRONOTES_REQUIRE(applied(source, in) == "- one\n  - two\n");
  MICRONOTES_REQUIRE(in.cursor == 11);

  const std::string nested = "- one\n  - two\n";
  const auto out = micronotes::doc::outdent(nested, 11);
  MICRONOTES_REQUIRE(applied(nested, out) == "- one\n- two\n");
  MICRONOTES_REQUIRE(out.cursor == 9);

  // Markdown has nothing for the first item to nest under, and nothing at the
  // margin to pull further left.
  MICRONOTES_REQUIRE(!micronotes::doc::indent(source, 3).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::outdent(source, 3).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::indent("plain text\n", 3).valid);
}

MICRONOTES_TEST(edits_move_a_block_past_its_neighbour) {
  const std::string source = "A\n\nB\n";
  const auto down = micronotes::doc::moveBlock(source, 0, 1);
  MICRONOTES_REQUIRE(applied(source, down) == "B\n\nA\n");
  MICRONOTES_REQUIRE(down.cursor == 3);

  const auto up = micronotes::doc::moveBlock(source, 3, -1);
  MICRONOTES_REQUIRE(applied(source, up) == "B\n\nA\n");
  MICRONOTES_REQUIRE(up.cursor == 0);

  MICRONOTES_REQUIRE(!micronotes::doc::moveBlock(source, 0, -1).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::moveBlock(source, 3, 1).valid);
}

MICRONOTES_TEST(edits_move_a_block_without_a_trailing_newline) {
  const std::string source = "A\n\nB";
  const auto up = micronotes::doc::moveBlock(source, 3, -1);
  MICRONOTES_REQUIRE(applied(source, up) == "B\n\nA");
}

MICRONOTES_TEST(edits_duplicate_and_delete_blocks) {
  const std::string source = "- one\n- two\n";
  const auto copy = micronotes::doc::duplicateBlock(source, 3);
  MICRONOTES_REQUIRE(applied(source, copy) == "- one\n- one\n- two\n");
  MICRONOTES_REQUIRE(copy.cursor == 9);

  const auto gone = micronotes::doc::deleteBlock(source, 3);
  MICRONOTES_REQUIRE(applied(source, gone) == "- two\n");
  MICRONOTES_REQUIRE(gone.cursor == 0);
}

MICRONOTES_TEST(edits_wrap_and_unwrap_a_selection) {
  const std::string source = "hello world";
  const auto bold = micronotes::doc::wrapSelection(source, 6, 11, "**", "**");
  MICRONOTES_REQUIRE(applied(source, bold) == "hello **world**");
  MICRONOTES_REQUIRE(bold.selects);
  MICRONOTES_REQUIRE(bold.anchor == 8);
  MICRONOTES_REQUIRE(bold.cursor == 13);

  // Toggling off works whether the markers sit outside or inside the selection.
  const std::string wrapped = "hello **world**";
  MICRONOTES_REQUIRE(applied(wrapped, micronotes::doc::wrapSelection(wrapped, 8, 13, "**", "**")) == "hello world");
  MICRONOTES_REQUIRE(applied(wrapped, micronotes::doc::wrapSelection(wrapped, 6, 15, "**", "**")) == "hello world");

  // With no selection the pair goes in and the caret waits between the markers.
  const auto empty = micronotes::doc::wrapSelection(source, 5, 5, "`", "`");
  MICRONOTES_REQUIRE(applied(source, empty) == "hello`` world");
  MICRONOTES_REQUIRE(empty.cursor == 6);
  MICRONOTES_REQUIRE(!empty.selects);
}

MICRONOTES_TEST(edits_make_a_link_from_a_selection) {
  const std::string source = "click here";
  const auto edit = micronotes::doc::makeLink(source, 6, 10);
  MICRONOTES_REQUIRE(applied(source, edit) == "click [here]()");
  MICRONOTES_REQUIRE(edit.cursor == 13);  // between the parentheses

  const auto targeted = micronotes::doc::makeLink(source, 6, 10, "note.md");
  MICRONOTES_REQUIRE(applied(source, targeted) == "click [here](note.md)");
}

MICRONOTES_TEST(edits_continue_lists_on_enter) {
  const std::string bullet = "- one\n";
  MICRONOTES_REQUIRE(applied(bullet, micronotes::doc::continueList(bullet, 5)) == "- one\n- \n");

  const std::string ordered = "1. one\n";
  MICRONOTES_REQUIRE(applied(ordered, micronotes::doc::continueList(ordered, 6)) == "1. one\n2. \n");

  // A continued task always starts unchecked.
  const std::string todo = "- [x] one\n";
  MICRONOTES_REQUIRE(applied(todo, micronotes::doc::continueList(todo, 9)) == "- [x] one\n- [ ] \n");

  const std::string quote = "> quoted\n";
  MICRONOTES_REQUIRE(applied(quote, micronotes::doc::continueList(quote, 8)) == "> quoted\n> \n");

  // Paragraphs get a plain newline: the caller inserts it.
  MICRONOTES_REQUIRE(!micronotes::doc::continueList("plain\n", 5).valid);
}

MICRONOTES_TEST(edits_continue_a_list_with_the_marker_it_was_written_with) {
  // Markdown has three bullet characters and two ordered delimiters, and the
  // one on screen is the one the author chose. Continuing `* a` with `- ` also
  // *ends* the list as far as the file is concerned -- a different bullet
  // character starts a new list -- so this was a correctness bug, not a
  // cosmetic one.
  const std::string star = "* one\n";
  MICRONOTES_REQUIRE(applied(star, micronotes::doc::continueList(star, 5)) == "* one\n* \n");

  const std::string plus = "+ one\n";
  MICRONOTES_REQUIRE(applied(plus, micronotes::doc::continueList(plus, 5)) == "+ one\n+ \n");

  const std::string paren = "1) one\n";
  MICRONOTES_REQUIRE(applied(paren, micronotes::doc::continueList(paren, 6)) == "1) one\n2) \n");

  const std::string task = "* [x] one\n";
  MICRONOTES_REQUIRE(applied(task, micronotes::doc::continueList(task, 9)) == "* [x] one\n* [ ] \n");

  // And a nested item, where the indentation is written by the same call.
  const std::string nested = "* one\n  * two\n";
  MICRONOTES_REQUIRE(applied(nested, micronotes::doc::continueList(nested, 13)) ==
                     "* one\n  * two\n  * \n");
}

MICRONOTES_TEST(edits_keep_the_bullet_character_across_a_list_kind_change) {
  using micronotes::doc::BlockKind;
  const std::string star = "* one\n";
  MICRONOTES_REQUIRE(applied(star, micronotes::doc::turnInto(star, 2, BlockKind::Todo, 0)) ==
                     "* [ ] one\n");

  // Ordered and unordered share no punctuation, so each falls back to its own
  // default rather than carrying the other's over.
  const std::string paren = "1) one\n";
  MICRONOTES_REQUIRE(applied(paren, micronotes::doc::turnInto(paren, 3, BlockKind::Bullet, 0)) ==
                     "- one\n");
  const std::string plus = "+ one\n";
  MICRONOTES_REQUIRE(applied(plus, micronotes::doc::turnInto(plus, 2, BlockKind::Ordered, 0)) ==
                     "1. one\n");

  // A paragraph has no marker of its own to keep.
  const std::string plain = "one\n";
  MICRONOTES_REQUIRE(applied(plain, micronotes::doc::turnInto(plain, 1, BlockKind::Bullet, 0)) ==
                     "- one\n");
}

MICRONOTES_TEST(edits_leave_the_list_on_an_empty_item) {
  // The blank line is the point: "- one\ntext" is a lazy continuation, so
  // dropping the marker alone would leave what is typed next inside the item.
  const std::string flat = "- one\n- \n";
  const auto exit = micronotes::doc::continueList(flat, 8);
  MICRONOTES_REQUIRE(applied(flat, exit) == "- one\n\n\n");
  MICRONOTES_REQUIRE(exit.cursor == 7);
  MICRONOTES_REQUIRE(micronotes::doc::scanBlocks(applied(flat, exit))[0].end() == 6);

  // A quote ends the same way, and for the same reason.
  const std::string quote = "> a\n> \n";
  const auto left = micronotes::doc::continueList(quote, 6);
  MICRONOTES_REQUIRE(applied(quote, left) == "> a\n\n\n");
  MICRONOTES_REQUIRE(left.cursor == 5);

  // A nested empty item steps out one level first.
  const std::string nested = "- one\n  - \n";
  MICRONOTES_REQUIRE(applied(nested, micronotes::doc::continueList(nested, 10)) == "- one\n- \n");
}

MICRONOTES_TEST(edits_close_an_open_fence) {
  const std::string source = "```js\n";
  const auto edit = micronotes::doc::closeFence(source, 5);
  MICRONOTES_REQUIRE(applied(source, edit) == "```js\n\n```\n");
  MICRONOTES_REQUIRE(edit.cursor == 6);

  // A fence that already closes needs nothing, and neither does anything else.
  MICRONOTES_REQUIRE(!micronotes::doc::closeFence("```\ncode\n```\n", 3).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::closeFence("plain\n", 5).valid);
}

MICRONOTES_TEST(edits_backspace_at_a_block_start_strips_the_marker) {
  const std::string heading = "## hi\n";
  const auto unwrap = micronotes::doc::outdentOrUnwrap(heading, 3);
  MICRONOTES_REQUIRE(applied(heading, unwrap) == "hi\n");
  MICRONOTES_REQUIRE(unwrap.cursor == 0);

  // A nested item outdents before it unwraps.
  const std::string nested = "- a\n  - b\n";
  MICRONOTES_REQUIRE(applied(nested, micronotes::doc::outdentOrUnwrap(nested, 8)) == "- a\n- b\n");

  // Anywhere but the first content byte, Backspace stays Backspace.
  MICRONOTES_REQUIRE(!micronotes::doc::outdentOrUnwrap(heading, 4).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::outdentOrUnwrap("plain\n", 0).valid);
}

MICRONOTES_TEST(edits_apply_the_task_typing_shortcut) {
  const std::string bullet = "- [] ";
  MICRONOTES_REQUIRE(applied(bullet, micronotes::doc::applyMarkdownShortcut(bullet, 5)) == "- [ ] ");
  const std::string paragraph = "[] ";
  const auto edit = micronotes::doc::applyMarkdownShortcut(paragraph, 3);
  MICRONOTES_REQUIRE(applied(paragraph, edit) == "- [ ] ");
  MICRONOTES_REQUIRE(edit.cursor == 6);
  const std::string done = "[x] ";
  MICRONOTES_REQUIRE(applied(done, micronotes::doc::applyMarkdownShortcut(done, 4)) == "- [x] ");

  // Everything else is already the Markdown the user meant, and is left alone.
  MICRONOTES_REQUIRE(!micronotes::doc::applyMarkdownShortcut("# ", 2).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::applyMarkdownShortcut("- ", 2).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::applyMarkdownShortcut("```\n[] \n```\n", 7).valid);
}

MICRONOTES_TEST(edits_undo_restores_the_buffer_byte_for_byte) {
  const std::string original = "# Title\n\n- one\n- two\n\n> quoted\n";
  micronotes::editor::MarkdownEditor editor;
  editor.setText(original);

  const auto run = [&](const Edit& edit) {
    MICRONOTES_REQUIRE(edit.valid);
    editor.replaceRange(edit.start, edit.end, edit.text);
  };
  run(micronotes::doc::indent(editor.text(), 17));
  run(micronotes::doc::toggleTodo(editor.text(), editor.cursor()));
  run(micronotes::doc::duplicateBlock(editor.text(), editor.cursor()));
  run(micronotes::doc::turnInto(editor.text(), 2, BlockKind::Heading, 3));
  run(micronotes::doc::moveBlock(editor.text(), 0, 1));
  MICRONOTES_REQUIRE(editor.text() != original);

  int steps = 0;
  while(editor.undo()) ++steps;
  MICRONOTES_REQUIRE(steps == 5);
  MICRONOTES_REQUIRE(editor.text() == original);

  // And redo puts every byte back.
  int forward = 0;
  while(editor.redo()) ++forward;
  MICRONOTES_REQUIRE(forward == 5);
  MICRONOTES_REQUIRE(editor.text() != original);
}

MICRONOTES_TEST(edits_treat_the_empty_last_line_as_its_own_block) {
  // The buffer's trailing newline leaves a caret position no block covers.
  // Enter there starts a paragraph rather than continuing the list above.
  const std::string source = "- one\n";
  MICRONOTES_REQUIRE(!micronotes::doc::continueList(source, 6).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::indent(source, 6).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::duplicateBlock(source, 6).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::deleteBlock(source, 6).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::moveBlock(source, 6, -1).valid);

  // Turning that empty line into a block appends rather than rewriting.
  const auto bullet = micronotes::doc::turnInto(source, 6, BlockKind::Bullet);
  MICRONOTES_REQUIRE(applied(source, bullet) == "- one\n- ");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::toggleTodo(source, 6)) == "- one\n- [ ] ");

  // Without the trailing newline the caret really is inside the last block.
  MICRONOTES_REQUIRE(micronotes::doc::continueList("- one", 5).valid);
}

MICRONOTES_TEST(edits_act_on_every_block_a_range_touches) {
  const std::string source = "one\n\ntwo\n\nthree\n";
  // A caret in the first block and one in the second: "three" is untouched.
  const auto bullets = micronotes::doc::turnBlocksInto(source, 1, 6, BlockKind::Bullet);
  MICRONOTES_REQUIRE(applied(source, bullets) == "- one\n\n- two\n\nthree\n");
  MICRONOTES_REQUIRE(bullets.selects);

  const auto gone = micronotes::doc::deleteBlocks(source, 1, 6);
  MICRONOTES_REQUIRE(applied(source, gone) == "three\n");
  MICRONOTES_REQUIRE(gone.cursor == 0);

  const auto copy = micronotes::doc::duplicateBlocks(source, 1, 6);
  MICRONOTES_REQUIRE(applied(source, copy) == "one\n\ntwo\n\none\n\ntwo\n\nthree\n");

  // A range of one block behaves like the single-block transforms.
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::deleteBlocks(source, 1, 1)) == "two\n\nthree\n");
}

// A multi-block turn-into is exactly the single-block turn-into applied to each
// block of the selection, and nothing else. It used to be written that way --
// rewriting one buffer in place, back to front, rescanning it once per block --
// and the rewrite that made the partition lendable has to produce the same
// bytes. Checked against the loop it replaced, over every kind the scanner
// models and every span of a mixed note.
//
// `selects` is what tells the two paths apart: only the range rewrite sets it,
// so an edit without it is the single-block fallback for a span that held
// nothing to rewrite.
MICRONOTES_TEST(edits_turn_a_range_into_what_turning_each_block_would) {
  const std::string source =
    "# Heading\n\ntext\n\n- a\n  - b\n\n1. one\n2. two\n\n- [ ] task\n- [x] done\n\n"
    "> quote\n> more\n\n```py\ncode\n```\n\n---\n\nlast\n";
  const BlockKind kinds[] = {BlockKind::Bullet, BlockKind::Todo,    BlockKind::Ordered,
                             BlockKind::Quote,  BlockKind::Heading, BlockKind::Code,
                             BlockKind::Paragraph};
  for(const BlockKind kind : kinds) {
    for(std::size_t from = 0; from <= source.size(); from += 3) {
      for(const std::size_t width : {std::size_t {0}, std::size_t {14}, std::size_t {60}}) {
        const std::size_t to = std::min(from + width, source.size());
        const auto ranged = micronotes::doc::turnBlocksInto(source, from, to, kind, 2);
        const std::string where = " at [" + std::to_string(from) + ", " + std::to_string(to) + ")";
        if(!ranged.selects) {
          // Either nothing to rewrite at all, or a span that held only blanks
          // and fell back to the caret's own block.
          const Edit fallback = micronotes::doc::turnInto(source, from, kind, 2);
          micronotes::tests::require(ranged.valid == fallback.valid, "fallback validity" + where);
          if(fallback.valid) {
            micronotes::tests::require(ranged.text == fallback.text, "fallback text" + where);
            micronotes::tests::require(ranged.start == fallback.start && ranged.end == fallback.end,
                                       "fallback bounds" + where);
          }
          continue;
        }

        // The loop this replaced: copy the span, walk its blocks back to front,
        // and rewrite each one in place against the buffer as it stands.
        std::string chunk = source.substr(ranged.start, ranged.end - ranged.start);
        const auto chunkBlocks = micronotes::doc::scanBlocks(chunk);
        bool changed = false;
        for(std::size_t i = chunkBlocks.size(); i-- > 0;) {
          if(chunkBlocks[i].kind == BlockKind::Blank) continue;
          const Edit one = micronotes::doc::turnInto(chunk, chunkBlocks[i].contentStart(), kind, 2);
          if(!one.valid) continue;
          chunk.replace(one.start, one.end - one.start, one.text);
          changed = true;
        }
        micronotes::tests::require(changed, "range rewrote nothing" + where);
        micronotes::tests::require(ranged.text == chunk,
                                   "range text" + where + ": got \"" + ranged.text + "\" want \"" +
                                       chunk + "\"");
        micronotes::tests::require(ranged.cursor == ranged.start + chunk.size(), "range cursor" + where);
        micronotes::tests::require(ranged.anchor == ranged.start, "range anchor" + where);
      }
    }
  }
}

MICRONOTES_TEST(edits_refuse_a_range_that_holds_nothing) {
  const std::string source = "one\n";
  MICRONOTES_REQUIRE(!micronotes::doc::deleteBlocks("", 0, 0).valid);
  // Every block is already a paragraph, so there is nothing to turn.
  MICRONOTES_REQUIRE(!micronotes::doc::turnBlocksInto(source, 0, 3, BlockKind::Paragraph).valid);
}

MICRONOTES_TEST(edits_move_blocks_without_merging_them) {
  // The blank line between two paragraphs has to travel with the block that
  // moves, or the two run together into one paragraph.
  const std::string source = "one\n\ntwo\n\nthree\n";
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::moveBlock(source, 5, -1)) == "two\n\none\n\nthree\n");
  MICRONOTES_REQUIRE(applied(source, micronotes::doc::moveBlock(source, 5, 1)) == "one\n\nthree\n\ntwo\n");

  // Two blocks at once, moved as a group.
  const auto pair = micronotes::doc::moveBlocks(source, 0, 5, 1);
  MICRONOTES_REQUIRE(applied(source, pair) == "three\n\none\n\ntwo\n");
  MICRONOTES_REQUIRE(pair.selects);

  // A tight list has no separator to carry.
  const std::string list = "- a\n- b\n- c\n";
  MICRONOTES_REQUIRE(applied(list, micronotes::doc::moveBlock(list, 6, -1)) == "- b\n- a\n- c\n");
}

MICRONOTES_TEST(edits_drop_a_block_at_a_chosen_boundary) {
  const std::string source = "one\n\ntwo\n\nthree\n";
  // Dropping "one" at the boundary before "three".
  const auto down = micronotes::doc::moveBlocksTo(source, 0, 0, 10);
  MICRONOTES_REQUIRE(applied(source, down) == "two\n\none\n\nthree\n");

  // Dropping "three" at the very top.
  const auto top = micronotes::doc::moveBlocksTo(source, 10, 10, 0);
  MICRONOTES_REQUIRE(applied(source, top) == "three\n\none\n\ntwo\n");

  // A drop inside the blocks being moved changes nothing.
  MICRONOTES_REQUIRE(!micronotes::doc::moveBlocksTo(source, 0, 0, 0).valid);
  MICRONOTES_REQUIRE(!micronotes::doc::moveBlocksTo(source, 0, 0, 2).valid);
}

MICRONOTES_TEST(edits_undo_a_range_transform_byte_for_byte) {
  const std::string original = "one\n\ntwo\n\nthree\n";
  micronotes::editor::MarkdownEditor editor;
  editor.setText(original);

  const auto run = [&editor](const Edit& edit) {
    MICRONOTES_REQUIRE(edit.valid);
    editor.replaceRange(edit.start, edit.end, edit.text);
  };
  run(micronotes::doc::turnBlocksInto(editor.text(), 1, 6, BlockKind::Todo));
  run(micronotes::doc::moveBlocks(editor.text(), 0, 8, 1));
  run(micronotes::doc::duplicateBlocks(editor.text(), 0, 0));
  MICRONOTES_REQUIRE(editor.text() != original);

  int steps = 0;
  while(editor.undo()) ++steps;
  MICRONOTES_REQUIRE(steps == 3);
  MICRONOTES_REQUIRE(editor.text() == original);
}

MICRONOTES_TEST(edits_insert_a_block_after_the_current_one) {
  // The new block lands past the separator, not inside it.
  const std::string loose = "one\n\ntwo\n";
  const auto todo = micronotes::doc::insertBlockAfter(loose, 1, BlockKind::Todo);
  MICRONOTES_REQUIRE(applied(loose, todo) == "one\n\n- [ ] \n\ntwo\n");
  MICRONOTES_REQUIRE(todo.cursor == 11);

  // A tight list has no separator to reproduce.
  const std::string list = "- a\n- b\n";
  MICRONOTES_REQUIRE(applied(list, micronotes::doc::insertBlockAfter(list, 2, BlockKind::Bullet)) == "- a\n- \n- b\n");

  // A fence opens with the caret already inside it.
  const auto code = micronotes::doc::insertBlockAfter(list, 2, BlockKind::Code);
  MICRONOTES_REQUIRE(applied(list, code) == "- a\n```\n\n```\n- b\n");
  MICRONOTES_REQUIRE(code.cursor == 8);

  // At the end of a note without a trailing newline the block still starts on a
  // line of its own.
  MICRONOTES_REQUIRE(applied("one", micronotes::doc::insertBlockAfter("one", 1, BlockKind::Quote)) == "one\n> \n");
}

MICRONOTES_TEST(edits_turn_a_blank_line_into_a_block) {
  // The slash menu erases the "/" it opened on, leaving a blank line behind.
  const std::string source = "one\n\n\ntwo\n";
  const auto todo = micronotes::doc::turnBlocksInto(source, 5, 5, BlockKind::Todo);
  MICRONOTES_REQUIRE(applied(source, todo) == "one\n\n- [ ] \ntwo\n");
}

// Every operation takes the partition either as a loan or by scanning for
// itself, and the two have to be the same operation. This is the invariant that
// lets the live page hand its own block list over -- and the one that would
// break silently if a future edit read something from the partition that the
// synthesised empty-last-line entry does not carry.
//
// Run over sources chosen for the edges: a note with and without a trailing
// newline, a caret at 0, at the end, and on the empty last line, and blocks of
// every kind the scanner models.
MICRONOTES_TEST(edits_lending_the_partition_changes_nothing) {
  const std::string sources[] = {
    "# Heading\n\ntext\n\n- a\n  - b\n\n1. one\n2. two\n\n- [ ] task\n- [x] done\n\n"
    "> quote\n> more\n\n> [!NOTE] callout\n\n```py\ncode\n```\n\n---\n\nlast\n",
    // The same without the trailing newline: the empty last line is not there,
    // so the partition has one fewer entry and every index shifts at the end.
    "# Heading\n\ntext\n\n- a\n  - b\n\n```py\ncode\n```\n\nlast",
    "",
    "\n",
    "- only\n",
  };

  const auto same = [](const Edit& a, const Edit& b) {
    return a.valid == b.valid && a.start == b.start && a.end == b.end && a.text == b.text &&
           a.cursor == b.cursor && a.anchor == b.anchor && a.selects == b.selects;
  };

  for(const std::string& source : sources) {
    const auto blocks = micronotes::doc::scanBlocks(source);
    for(std::size_t caret = 0; caret <= source.size(); ++caret) {
      using namespace micronotes::doc;
      MICRONOTES_REQUIRE(same(continueList(source, caret), continueList(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(closeFence(source, caret), closeFence(source, caret, blocks)));
      MICRONOTES_REQUIRE(
        same(outdentOrUnwrap(source, caret), outdentOrUnwrap(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(toggleTodo(source, caret), toggleTodo(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(indent(source, caret), indent(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(outdent(source, caret), outdent(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(applyMarkdownShortcut(source, caret),
                              applyMarkdownShortcut(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(deleteBlock(source, caret), deleteBlock(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(duplicateBlock(source, caret), duplicateBlock(source, caret, blocks)));
      MICRONOTES_REQUIRE(same(moveBlock(source, caret, -1), moveBlock(source, caret, -1, blocks)));
      MICRONOTES_REQUIRE(same(moveBlock(source, caret, 1), moveBlock(source, caret, 1, blocks)));
      MICRONOTES_REQUIRE(same(turnInto(source, caret, BlockKind::Quote),
                              turnInto(source, caret, BlockKind::Quote, 1, blocks)));
      MICRONOTES_REQUIRE(same(insertBlockAfter(source, caret, BlockKind::Todo),
                              insertBlockAfter(source, caret, BlockKind::Todo, 1, blocks)));
      MICRONOTES_REQUIRE(same(moveBlocksTo(source, caret, caret, 0),
                              moveBlocksTo(source, caret, caret, 0, blocks)));
      // The range operations, over a span rather than a point.
      const std::size_t to = std::min(caret + 12, source.size());
      MICRONOTES_REQUIRE(same(deleteBlocks(source, caret, to), deleteBlocks(source, caret, to, blocks)));
      MICRONOTES_REQUIRE(
        same(duplicateBlocks(source, caret, to), duplicateBlocks(source, caret, to, blocks)));
      MICRONOTES_REQUIRE(same(moveBlocks(source, caret, to, 1), moveBlocks(source, caret, to, 1, blocks)));
      MICRONOTES_REQUIRE(same(turnBlocksInto(source, caret, to, BlockKind::Bullet),
                              turnBlocksInto(source, caret, to, BlockKind::Bullet, 1, blocks)));
    }
  }
}
