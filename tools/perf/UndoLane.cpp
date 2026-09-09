#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "core/editor/MarkdownEditor.h"

#include <cstddef>
#include <iostream>
#include <string>


namespace micronotes::perfharness {

// The undo lane.
//
// Every other lane in this file measures a *rate* -- what a keystroke, a frame
// or a save costs. Undo is not a rate: it is a **ceiling**, a thing the editor
// holds on to for as long as the note is open, and the only number in this file
// shaped like it is `peak_rss` at the very bottom, which covers the whole
// process and so cannot attribute anything to the editor.
//
// That is why nothing here saw an undo history costing a hundred times the note
// it belonged to. `editor_undo_history_is_bounded` asserted a byte figure and
// drove it with a 4 KB buffer, so it measured 400 KB against a 32 MB assertion;
// the harness measured nothing at all. A ceiling needs a lane that reports the
// ceiling, driven at a size where the ceiling is what runs out.
//
// So this lane reports **bytes retained**, next to the size of the note they
// belong to, which is the comparison that makes the number mean something: a
// history that costs a multiple of the note is a history storing the note.

// What a session's undo history may retain, as a multiple of the note.
//
// The floor under any scheme is one record per step -- a few dozen bytes of
// offsets and caret positions -- so on a small note the interesting question is
// whether the *note* is in there. 32 KB over a 2 KB note is 16x, which a splice
// history clears by a wide margin and a snapshot history cannot come close to.
static constexpr std::size_t kUndoSmallNoteBytes = 32u * 1024u;
// The same session on a 200 KB note. Retention that does not grow with the
// note's size is the whole point: this budget is the *same* order as the one
// above even though the note is a hundred times bigger.
static constexpr std::size_t kUndoLargeNoteBytes = 64u * 1024u;
// Deleting text is the one case a history has to keep bytes: what came out is
// the only copy left. Forty whole-note replacements of a 200 KB note is 8 MB of
// genuinely irreducible history, and this is the ceiling that has to bite.
static constexpr std::size_t kUndoReplacementBytes = 9u * 1024u * 1024u;
// One Ctrl+Z on a 200 KB note. It used to be a whole-buffer copy onto the redo
// stack plus a whole-buffer word recount; it is a splice now, and the budget is
// the keystroke's.
static constexpr std::uint64_t kUndoStepBudgetMicros = 2000;

namespace {

struct UndoSession {
  std::size_t retainedBytes = 0;
  std::size_t depth = 0;
};

// A session: `edits` deliberate edits, each sealed off from the last by a caret
// move, which is what a person does when they fix a word here and a word there.
// Sealed rather than coalesced on purpose -- a coalesced run is one record and
// would measure the coalescing rather than the retention.
static UndoSession undoSession(std::size_t noteBytes, int edits) {
  microcore::editor::MarkdownEditor editor;
  editor.setText(std::string(noteBytes, 'x'));
  for(int i = 0; i < edits; ++i) {
    editor.moveCursor(static_cast<std::size_t>(i) % (noteBytes + 1));
    editor.insert("a");
  }
  return {editor.undoBytes(), editor.undoDepth()};
}

}

bool undoBudgets() {
  std::cout << "\n=== what an editing session retains for undo ===\n";
  bool ok = true;
  const auto report = [&](const char* name, const UndoSession& session, std::size_t noteBytes,
                          std::size_t budget) {
    std::printf("%-40s %10.1f KB retained %6zu steps %8.1f KB note %6.2fx\n", name,
                static_cast<double>(session.retainedBytes) / 1024.0, session.depth,
                static_cast<double>(noteBytes) / 1024.0,
                static_cast<double>(session.retainedBytes) / static_cast<double>(noteBytes));
    if(session.retainedBytes <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " retains " << session.retainedBytes
              << " bytes, over " << budget << "\n";
    ok = false;
  };

  // A small note, edited for a long time. This is the case the count ceiling
  // was written for and the one it governs, so what it retains is the depth
  // times whatever a step costs -- which is the whole question.
  report("undo.session_2kb_note", undoSession(2u * 1024u, 200), 2u * 1024u, kUndoSmallNoteBytes);
  // The same session, a hundred times the note.
  report("undo.session_200kb_note", undoSession(200u * 1024u, 400), 200u * 1024u,
         kUndoLargeNoteBytes);

  // Whole-note replacements: the deleted bytes are real history and cannot be
  // dropped, so this is the scenario the byte ceiling exists for. It must stay
  // bounded, and it must still be an undo history at the end of it.
  {
    const std::size_t noteBytes = 200u * 1024u;
    microcore::editor::MarkdownEditor editor;
    editor.setText(std::string(noteBytes, 'x'));
    for(int i = 0; i < 80; ++i) {
      editor.selectAll();
      editor.insert(std::string(noteBytes, static_cast<char>('a' + (i % 26))));
    }
    report("undo.whole_note_replacements", {editor.undoBytes(), editor.undoDepth()}, noteBytes,
           kUndoReplacementBytes);
    if(editor.undoDepth() < 8) {
      std::cerr << "BUDGET FAILED: undo.whole_note_replacements kept only " << editor.undoDepth()
                << " steps\n";
      ok = false;
    }
  }

  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // And what one step *costs*, which is the other half of the story: a history
  // of whole-buffer snapshots is not only large, it charges a copy of the note
  // and a full word recount for every Ctrl+Z.
  {
    microcore::editor::MarkdownEditor editor;
    editor.setText(std::string(200u * 1024u, 'x'));
    for(int i = 0; i < 64; ++i) {
      editor.moveCursor(static_cast<std::size_t>(i) * 512u);
      editor.insert("a");
    }
    gate("undo.step_200kb_note", measureIterations("undo.step_200kb_note", 32, [&](int) {
           if(!editor.undo()) editor.redo();
         }),
         kUndoStepBudgetMicros);
    while(editor.redo()) {
    }
    gate("redo.step_200kb_note", measureIterations("redo.step_200kb_note", 32, [&](int) {
           if(!editor.undo()) editor.redo();
         }),
         kUndoStepBudgetMicros);
  }
  return ok;
}

}
