#include "TestSupport.h"
#include "ShellFixture.h"

#include "app/RawPane.h"
#include "app/Shell.h"
#include "core/editor/SoftWrap.h"
#include "ui/TextRenderer.h"

#include <string>
#include <vector>

// The raw pane's soft wrap, through the shell that owns it.
//
// `softWrapUpdate` is proved against a full rewrap in `EditorSoftWrapTests`.
// What that cannot prove is the *wiring*: the pane only takes the incremental
// path when the memo's standing key, the editor's `lastChange()` and the
// current revision line up, and every one of those is on this side. Get one
// wrong and the pane keeps a wrap of some earlier buffer -- which is a caret in
// the wrong place and a highlight over the wrong bytes, with nothing failing.

namespace {

using micronotes::editor::SoftWrapRow;

bool sameRows(const std::vector<SoftWrapRow>& a, const std::vector<SoftWrapRow>& b) {
  if(a.size() != b.size()) return false;
  for(std::size_t i = 0; i < a.size(); ++i) {
    if(a[i].start != b[i].start || a[i].end != b[i].end) return false;
  }
  return true;
}

std::string wrapFixtureBody() {
  std::string body = "# Raw pane\n\n";
  for(int paragraph = 0; paragraph < 12; ++paragraph) {
    body += "The quick brown fox jumps over the lazy dog while the pane wraps it to "
            "whatever column the window happens to give, which is the whole point.\n\n";
    body += "- a list item with `code` and **bold** in it\n";
    body += "- another, longer, so that it certainly has to break somewhere\n\n";
  }
  return body;
}

}

MICRONOTES_TEST(shell_raw_pane_wrap_tracks_the_buffer_through_a_run_of_keystrokes) {
  micronotes::app::UiRuntime ui;
  const micronotes::tests::ScratchNote note(ui, "raw-pane-wrap", wrapFixtureBody());
  micronotes::ui::TextRenderer text(nullptr);
  const micronotes::ui::Rect pane {0.0f, 0.0f, 520.0f, 800.0f};

  const auto full = [&] {
    const micronotes::ui::Rect writing = micronotes::app::editorWritingRect(pane);
    const int width = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
    return micronotes::editor::softWrap(ui.editor.text(), width, [&](std::string_view value) {
      return text.width(value, false, true);
    });
  };

  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));

  // Typing: an insert of one byte, over and over, which is the path the memo
  // used to miss on and rebuild through.
  ui.editor.moveTo(400, false);
  for(int i = 0; i < 12; ++i) {
    ui.editor.insert("x");
    MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));
  }

  // A line break, which turns one logical line into two -- the case where the
  // update produces a different number of rows than it replaced.
  ui.editor.insert("\n");
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));

  // Backspace over it again, which joins them back.
  ui.editor.erasePrevious();
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));

  // A structural edit near the top: everything after it shifts.
  ui.editor.replaceRange(0, 10, "## A much longer heading than the one that was here\n");
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));

  // Undo and redo, which the editor reports as ordinary splices.
  MICRONOTES_REQUIRE(ui.editor.undo());
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));
  MICRONOTES_REQUIRE(ui.editor.redo());
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));

  // A whole-buffer replacement, which reports the whole buffer and so bounds
  // nothing -- the pane must rebuild rather than splice.
  ui.editor.setText("one\ntwo\nthree\n");
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), full()));
}

// Two edits between two asks: the standing rows describe neither buffer, and
// the pane's own check is what has to catch it. Without it the wrap would be
// spliced against a partition of some earlier note.
MICRONOTES_TEST(shell_raw_pane_wrap_rebuilds_when_it_missed_an_edit) {
  micronotes::app::UiRuntime ui;
  const micronotes::tests::ScratchNote note(ui, "raw-pane-skip", wrapFixtureBody());
  micronotes::ui::TextRenderer text(nullptr);
  const micronotes::ui::Rect pane {0.0f, 0.0f, 520.0f, 800.0f};

  micronotes::app::rawPaneRows(text, ui, pane);
  ui.editor.moveTo(200, false);
  ui.editor.insert("first insert, long enough to move a line break ");
  ui.editor.insert("\nand a second, with a break in it\n");

  const micronotes::ui::Rect writing = micronotes::app::editorWritingRect(pane);
  const int width = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
  const auto expected =
    micronotes::editor::softWrap(ui.editor.text(), width, [&](std::string_view value) {
      return text.width(value, false, true);
    });
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), expected));
}

// The same, with a missed edit that does not change the buffer's length. The
// O(1) staleness check inside `softWrapUpdate` compares sizes and so cannot see
// this one at all: the pane's own revision check is the only thing between the
// reader and a wrap spliced against a buffer that no longer exists.
MICRONOTES_TEST(shell_raw_pane_wrap_rebuilds_when_the_edit_it_missed_kept_the_length) {
  micronotes::app::UiRuntime ui;
  const micronotes::tests::ScratchNote note(ui, "raw-pane-samelen", wrapFixtureBody());
  micronotes::ui::TextRenderer text(nullptr);
  const micronotes::ui::Rect pane {0.0f, 0.0f, 520.0f, 800.0f};

  micronotes::app::rawPaneRows(text, ui, pane);
  // Two edits at two places, each the same byte count it replaced -- and the
  // first puts a line break where there was none, so the rows either side of it
  // are a different partition entirely.
  ui.editor.replaceRange(20, 60, std::string(19, 'a') + "\n" + std::string(20, 'b'));
  ui.editor.replaceRange(400, 470, std::string(70, 'w'));

  const micronotes::ui::Rect writing = micronotes::app::editorWritingRect(pane);
  const int width = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
  const auto expected =
    micronotes::editor::softWrap(ui.editor.text(), width, [&](std::string_view value) {
      return text.width(value, false, true);
    });
  MICRONOTES_REQUIRE(sameRows(micronotes::app::rawPaneRows(text, ui, pane), expected));
}

// The column moving is not an edit, so the standing rows are wrong for a reason
// the edit stamps cannot describe. The memo's key is what catches that, and it
// carries the width and the text size for exactly this.
MICRONOTES_TEST(shell_raw_pane_wrap_follows_the_column_and_not_only_the_revision) {
  micronotes::app::UiRuntime ui;
  const micronotes::tests::ScratchNote note(ui, "raw-pane-column", wrapFixtureBody());
  micronotes::ui::TextRenderer text(nullptr);

  const auto rowsAt = [&](float w) {
    const micronotes::ui::Rect pane {0.0f, 0.0f, w, 800.0f};
    const auto& got = micronotes::app::rawPaneRows(text, ui, pane);
    const micronotes::ui::Rect writing = micronotes::app::editorWritingRect(pane);
    const int width = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
    const auto want =
      micronotes::editor::softWrap(ui.editor.text(), width, [&](std::string_view value) {
        return text.width(value, false, true);
      });
    return sameRows(got, want);
  };

  MICRONOTES_REQUIRE(rowsAt(520.0f));
  MICRONOTES_REQUIRE(rowsAt(300.0f));
  MICRONOTES_REQUIRE(rowsAt(900.0f));
  ui.editor.insert("z");
  MICRONOTES_REQUIRE(rowsAt(900.0f));
  MICRONOTES_REQUIRE(rowsAt(520.0f));
}
