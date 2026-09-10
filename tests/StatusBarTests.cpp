#include "TestSupport.h"
#include "ShellFixture.h"

#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/Shell.h"
#include "app/StatusBar.h"
#include "app/StatusLine.h"

#include <filesystem>
#include <string>

// The status bar as a value. Every segment's text, tone and command comes out
// of one pure-over-the-shell function, which is what lets the strip be checked
// without a window -- and what stopped the paint deciding a colour by reading
// its own label back.

using micronotes::app::CaretPlace;
using micronotes::app::StatusSegment;
using micronotes::app::StatusTone;
using micronotes::app::UiRuntime;
using micronotes::app::caretPlaceFrom;
using micronotes::app::caretPlaceIn;
using micronotes::app::codePointsIn;
using micronotes::app::statusSegments;

namespace {

const micronotes::app::StatusSegmentValue& segment(const micronotes::app::StatusSegments& all,
                                                   StatusSegment which) {
  return all[static_cast<std::size_t>(which)];
}


}

MICRONOTES_TEST(status_bar_counts_lines_and_columns_from_one) {
  MICRONOTES_REQUIRE(caretPlaceIn("", 0).line == 1);
  MICRONOTES_REQUIRE(caretPlaceIn("", 0).column == 1);

  const std::string text = "one\ntwo\nthree";
  MICRONOTES_REQUIRE(caretPlaceIn(text, 0).line == 1);
  MICRONOTES_REQUIRE(caretPlaceIn(text, 3).column == 4);
  // The newline itself belongs to the line it ends; the byte after it starts
  // the next one at column 1.
  MICRONOTES_REQUIRE(caretPlaceIn(text, 4).line == 2);
  MICRONOTES_REQUIRE(caretPlaceIn(text, 4).column == 1);
  MICRONOTES_REQUIRE(caretPlaceIn(text, 8).line == 3);
  // Past the end clamps rather than walking off it.
  MICRONOTES_REQUIRE(caretPlaceIn(text, 9999).line == 3);
}

// Columns in code points, not bytes. A column of 14 on a line of seven accented
// letters is a number about the file's encoding rather than about where the
// caret is.
MICRONOTES_TEST(status_bar_columns_count_characters_not_bytes) {
  const std::string accented = "\xc3\xa9\xc3\xa9\xc3\xa9";  // three two-byte letters
  MICRONOTES_REQUIRE(accented.size() == 6);
  MICRONOTES_REQUIRE(caretPlaceIn(accented, 6).column == 4);
  MICRONOTES_REQUIRE(codePointsIn(accented, 0, 6) == 3);
  MICRONOTES_REQUIRE(codePointsIn(accented, 2, 6) == 2);
  MICRONOTES_REQUIRE(codePointsIn(accented, 4, 2) == 0);
}

MICRONOTES_TEST(status_bar_says_whether_the_note_is_saved) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-status-save", "hello there\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));

  auto clean = statusSegments(ui);
  MICRONOTES_REQUIRE(segment(clean, StatusSegment::Save).text == "Saved");
  MICRONOTES_REQUIRE(segment(clean, StatusSegment::Save).tone == StatusTone::Normal);
  // The tone is derived where the value is known, so the paint never has to
  // read the label back to pick a colour.
  ui.editor.insert("!");
  auto dirty = statusSegments(ui);
  MICRONOTES_REQUIRE(segment(dirty, StatusSegment::Save).text == "Unsaved");
  MICRONOTES_REQUIRE(segment(dirty, StatusSegment::Save).tone == StatusTone::Unsaved);
  // Clickable either way: "is it saved" is the question this segment answers,
  // and pressing it is how a reader makes the answer yes.
  MICRONOTES_REQUIRE(segment(dirty, StatusSegment::Save).clickable());
}

MICRONOTES_TEST(status_bar_reports_the_caret_and_the_selection) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-status-caret", "one\ntwo three\n");
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Editor);
  ui.editor.moveCursor(8);  // line 2, after "two t"

  auto plain = statusSegments(ui);
  MICRONOTES_REQUIRE(segment(plain, StatusSegment::Position).visible);
  MICRONOTES_REQUIRE(segment(plain, StatusSegment::Position).text == "Ln 2, Col 5");
  // Nothing selected, nothing said. A segment reading "0 characters selected"
  // is a column spent on the absence of a thing.
  MICRONOTES_REQUIRE(!segment(plain, StatusSegment::Selection).visible);

  ui.editor.selectRange(4, 7);
  auto selected = statusSegments(ui);
  MICRONOTES_REQUIRE(segment(selected, StatusSegment::Selection).visible);
  MICRONOTES_REQUIRE(segment(selected, StatusSegment::Selection).text == "3 characters selected");
  ui.editor.selectRange(4, 5);
  MICRONOTES_REQUIRE(segment(statusSegments(ui), StatusSegment::Selection).text ==
                     "1 character selected");
}

// A line and a column are coordinates in the note's Markdown, so they appear
// where the Markdown is: the raw pane and the split. The reading pane has no
// caret at all, and in the reading pane "Ln 42" names a line of a file the
// reader cannot see.
MICRONOTES_TEST(status_bar_reports_the_caret_where_the_source_is_on_screen) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-status-reading", "one\ntwo\n");
  const auto positionShows = [&](micronotes::ui::PaneMode mode) {
    ui.state.editWorkspace().setPaneMode(mode);
    return segment(statusSegments(ui), StatusSegment::Position).visible;
  };
  MICRONOTES_REQUIRE(positionShows(micronotes::ui::PaneMode::Editor));
  MICRONOTES_REQUIRE(positionShows(micronotes::ui::PaneMode::Split));
  MICRONOTES_REQUIRE(!positionShows(micronotes::ui::PaneMode::Viewer));
}

MICRONOTES_TEST(status_bar_names_the_view_and_offers_to_cycle_it) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-status-pane", "a word or two here\n");
  const auto segments = statusSegments(ui);
  MICRONOTES_REQUIRE(segment(segments, StatusSegment::PaneMode).text == std::string("Split"));
  MICRONOTES_REQUIRE(segment(segments, StatusSegment::PaneMode).command == "cycle-pane");
  MICRONOTES_REQUIRE(segment(segments, StatusSegment::Words).text == "5 words");
  // Reporting only. A segment that lifts under the pointer and does nothing
  // when pressed is worse than one that never invited the press.
  MICRONOTES_REQUIRE(!segment(segments, StatusSegment::Words).clickable());
  MICRONOTES_REQUIRE(!segment(segments, StatusSegment::Position).clickable());
}

// With nothing open there is nothing true about a note to report, so the bar
// says nothing rather than reporting zeroes about a note that is not there.
MICRONOTES_TEST(status_bar_is_empty_with_no_note_open) {
  UiRuntime ui;
  const auto segments = statusSegments(ui);
  for(const auto& value : segments) MICRONOTES_REQUIRE(!value.visible);
}

// The message channel: set at a hundred and forty sites with a plain
// assignment, shown for a few seconds, and then gone. The timestamp is stamped
// by the assignment rather than by a setter somebody can forget to call.
MICRONOTES_TEST(status_line_messages_are_transient) {
  micronotes::app::StatusLine status;
  MICRONOTES_REQUIRE(!status.showing(0));
  MICRONOTES_REQUIRE(status.lingerMs(0) == -1);

  status = "Copied selection";
  MICRONOTES_REQUIRE(status.text == "Copied selection");
  MICRONOTES_REQUIRE(status.at != 0);
  const Uint64 set = status.at;
  MICRONOTES_REQUIRE(status.showing(set));
  MICRONOTES_REQUIRE(status.showing(set + micronotes::app::kStatusLingerMs - 1));
  MICRONOTES_REQUIRE(!status.showing(set + micronotes::app::kStatusLingerMs));
  // The run loop sleeps until something happens, so the moment the message
  // stops being worth showing has to be a deadline it knows about.
  MICRONOTES_REQUIRE(status.lingerMs(set) == static_cast<int>(micronotes::app::kStatusLingerMs));
  MICRONOTES_REQUIRE(status.lingerMs(set + micronotes::app::kStatusLingerMs) == -1);
}

// The walked answer, against the only oracle that matters: the full one.
//
// `caretPlaceFrom` is right exactly when it gives what a scan from the note's
// first byte gives, from *every* anchor -- so the test is that equality over
// every (anchor, caret) pair of a small buffer with the shapes that have an
// edge in them in it: an empty first line, consecutive breaks, a trailing
// break, and multi-byte characters, which the column counts in code points.
MICRONOTES_TEST(status_caret_place_walked_from_any_anchor_matches_a_full_scan) {
  const std::string text = "\nalpha\n\nbe\xC3\xA9ta\ngamma\n\n";
  for(std::size_t anchor = 0; anchor <= text.size(); ++anchor) {
    const CaretPlace from = caretPlaceIn(text, anchor);
    for(std::size_t cursor = 0; cursor <= text.size(); ++cursor) {
      const CaretPlace walked = caretPlaceFrom(text, cursor, from);
      const CaretPlace whole = caretPlaceIn(text, cursor);
      MICRONOTES_REQUIRE(walked.line == whole.line);
      MICRONOTES_REQUIRE(walked.column == whole.column);
      MICRONOTES_REQUIRE(walked.lineStart == whole.lineStart);
    }
  }
}

// The same over a buffer with no line breaks at all, which is the case where
// the backwards walk finds nothing and has to leave the line where it was
// rather than stepping past the start of the note.
MICRONOTES_TEST(status_caret_place_walked_over_a_single_line) {
  const std::string text = "one long line and nothing else";
  for(std::size_t anchor = 0; anchor <= text.size(); ++anchor) {
    const CaretPlace from = caretPlaceIn(text, anchor);
    for(std::size_t cursor = 0; cursor <= text.size(); ++cursor) {
      const CaretPlace walked = caretPlaceFrom(text, cursor, from);
      MICRONOTES_REQUIRE(walked.line == 1);
      MICRONOTES_REQUIRE(walked.column == caretPlaceIn(text, cursor).column);
    }
  }
}

// The bar through the shell, over a long run of caret moves and edits, against
// the position a full scan would report.
//
// The walk is only sound if the standing place really is an anchor in the
// buffer being asked about, and that check is on the bar's side: the memo can
// only say "not this key". Get it wrong and the readout is a line number off
// by however many breaks the edit moved -- which nothing else in the shell
// notices, because a number that is merely wrong still fits in the segment.
MICRONOTES_TEST(status_bar_caret_readout_survives_editing_above_the_caret) {
  UiRuntime ui;
  std::string body;
  for(int i = 0; i < 200; ++i) body += "line " + std::to_string(i) + " of the note\n";
  const micronotes::tests::ScratchNote scratch_ui(ui, "micronotes-status-anchor", body);
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Editor);

  const auto reported = [&] { return segment(statusSegments(ui), StatusSegment::Position).text; };
  const auto expected = [&] {
    const CaretPlace place = caretPlaceIn(ui.editor.text(), ui.editor.cursor());
    return "Ln " + std::to_string(place.line) + ", Col " + std::to_string(place.column);
  };

  // Caret moves alone: the buffer does not change, so every one of these must
  // walk from the last answer rather than rescan, and every one must agree.
  for(const std::size_t at : {0u, 40u, 41u, 39u, 1200u, 1199u, 17u, 2400u, 0u}) {
    ui.editor.moveCursor(at);
    MICRONOTES_REQUIRE(reported() == expected());
  }

  // Typing on the caret's own line: the anchor survives, because the edit is
  // above its line start.
  ui.editor.moveCursor(1205);
  for(int i = 0; i < 8; ++i) {
    ui.editor.insert("x");
    MICRONOTES_REQUIRE(reported() == expected());
  }

  // A line break typed at the caret: the caret's line number goes up by one.
  ui.editor.insert("\n");
  MICRONOTES_REQUIRE(reported() == expected());

  // An edit *above* the caret's line, which moves every line number under it.
  // The anchor is no longer one and the bar has to say so.
  ui.editor.replaceRange(0, 20, "a\nb\nc\nd\ne\nf\n");
  ui.editor.moveCursor(1400);
  MICRONOTES_REQUIRE(reported() == expected());

  // A deletion above it, the other direction.
  ui.editor.replaceRange(0, 12, "");
  ui.editor.moveCursor(1400);
  MICRONOTES_REQUIRE(reported() == expected());

  // And a whole-buffer replacement, which is an anchor for nothing.
  ui.editor.setText("only\ntwo\n");
  ui.editor.moveCursor(6);
  MICRONOTES_REQUIRE(reported() == expected());
}
