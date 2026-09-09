#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "app/FindBar.h"
#include "app/PageView.h"
#include "app/RawPane.h"
#include "app/RightPanel.h"
#include "app/Shell.h"
#include "app/StatusBar.h"
#include "ui/AppState.h"
#include "ui/TextRenderer.h"

#include <filesystem>
#include <iostream>
#include <string>


namespace micronotes::perfharness {

// The shell lane: one keystroke, through the models the shell keeps.
//
// Every other edit lane here drives `doc::Layout` directly, and that gap cost
// two findings in the ninth pass, each larger than the layout work the budgets
// did measure: the outline panel rebuilt its block partition on every keystroke
// (240 us on a 200 KB note) and the status bar recounted the whole note on
// every keystroke (247 us), against a keystroke whose layout update is 14 us.
// Both shipped, both were invisible, and both were found by reading code.
//
// The shape is specific and it will recur: anything memoised on
// `ui.editor.revision()` is *by construction* recomputed on every keystroke,
// and the memo makes it look handled. This lane is the instrument for that
// shape. It drives a real `UiRuntime` -- a real editor, a real live page, the
// real right-hand panel -- and stops short of the paint: no window, no
// textures, no present. Everything above the paint is where all three findings
// were.
//
// Order matters and is the app's. `drawApp` lays the content out and draws the
// right panel afterwards, because the outline borrows the block partition the
// live page splices and `blocksAt` refuses to hand over one from a revision the
// layout has not reached. A lane that asked in the other order would measure
// the scan and call it the cost of the panel.
//
// The scenarios are separate so a regression is attributable to a surface, and
// `shell.keystroke` is their sum: the number a person typing actually pays.
static constexpr std::uint64_t kShellEditBudgetMicros = 200;
static constexpr std::uint64_t kShellOutlineBudgetMicros = 400;
// The status bar. It was 50 when the bar's whole job was a word count the
// editor already carried; the segments added one derived readout that is a pass
// over the note -- the caret's line and column -- memoised on the buffer's
// revision and the caret, which means a keystroke pays for it exactly once. On
// a 200 KB note that pass is tens of microseconds, so the budget moves to cover
// it and no further: a regression to per-frame, or to a second uncached walk,
// shows up here rather than in the total.
static constexpr std::uint64_t kShellStatusBudgetMicros = 250;
// The find bar's scan, which runs once per (buffer, needle, options) -- so
// once per keystroke while the bar is open, over the whole note. Its own
// scenario because it is the one surface whose cost the reader opts into, and
// because there was no lane for it at all when it was three separate scans: the
// page's, the raw pane's and the status line's, none of which the harness saw.
static constexpr std::uint64_t kShellFindBudgetMicros = 2000;
// The live page over a real face, which is the one part of a keystroke that
// shapes glyphs. Loose for the reason the font lane is loose: shaping is the
// scenario whose cost moves most with what else the machine is doing.
static constexpr std::uint64_t kShellPageBudgetMicros = 20000;
// The raw pane rewraps the whole note on every keystroke and has no incremental
// form. What it no longer does is *shape* anything to find out where a line
// ends: the pane is a monospaced grid, so a run of ASCII is as wide as it is
// long. That took it from 70 ms to 0.7 ms, and a budget in the hundreds of
// microseconds is a budget again rather than a ceiling on a number nobody was
// pleased with.
static constexpr std::uint64_t kShellRawBudgetMicros = 3000;
static constexpr std::uint64_t kShellKeystrokeBudgetMicros = 24000;

bool shellBudgets(const std::filesystem::path& root, const std::string& body) {
  micronotes::ui::TextRenderer text(nullptr);
  if(!text.fonts().ready()) {
    std::cout << "\n=== a keystroke through the shell === (skipped: no usable face)\n";
    return true;
  }
  std::cout << "\n=== a keystroke through the shell ===\n";

  micronotes::app::UiRuntime ui;
  if(!ui.state.openOrCreateLibrary(root)) {
    std::cerr << "shell lane: could not open the fixture library\n";
    return false;
  }
  const auto notes = ui.state.catalog().notes();
  if(notes.empty()) {
    std::cerr << "shell lane: the fixture library has no notes\n";
    return false;
  }
  ui.state.selectNote(notes.front().id);
  ui.editor.setText(body);

  // The two hooks a `Complex` block needs. md4c is not wired here, so those
  // blocks measure zero and lay out empty -- which is what an unwired page
  // already answers, and is the same stand-in the font lane uses.
  micronotes::app::PageViewHooks hooks;
  hooks.measureComplex = [](const micronotes::doc::SourceBlock&, float) { return 0.0f; };
  ui.livePage.setHooks(std::move(hooks));

  const micronotes::ui::Rect page {0.0f, 0.0f, 900.0f, 700.0f};
  std::size_t caret = body.find("A paragraph", body.size() / 2);
  if(caret == std::string::npos) caret = body.size() / 2;
  ui.editor.moveCursor(caret);

  // One keystroke, as the shell performs it: the buffer changes, then each
  // surface is asked for what it shows.
  const auto type = [&] {
    ui.editor.insert("x");
  };
  const auto layoutPage = [&] {
    micronotes::app::PageFrame frame;
    frame.sourceRevision = ui.editor.revision();
    frame.editedSpan = ui.editor.lastChange();
    ui.livePage.beginFrame(frame);
    ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), page);
  };
  const auto askOutline = [&] { sink += micronotes::app::outlineFor(ui).size(); };
  // Through the bar's own model rather than the two counts it used to be: the
  // segments are where the work is now, and a lane that measures the inputs
  // instead of the answer is a lane that cannot see a readout being added.
  const auto askStatus = [&] {
    for(const auto& segment : micronotes::app::statusSegments(ui)) sink += segment.text.size();
  };
  const auto askFind = [&] { micronotes::app::refreshFindMatches(ui); };
  const auto askRawPane = [&] { sink += micronotes::app::rawPaneRows(text, ui, page).size(); };

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Each surface on its own, with the rest of the keystroke performed untimed
  // around it -- so what is measured is that surface's answer to a buffer that
  // has just moved, not a memo hit left over from the scenario before.
  gate("shell.edit", measureIterations("shell.edit", 24,
                                       [&](int) {
                                         type();
                                       }),
       kShellEditBudgetMicros);
  layoutPage();

  gate("shell.live_page", measureIterations("shell.live_page", 24,
                                            [&](int) {
                                              type();
                                              layoutPage();
                                            }),
       kShellPageBudgetMicros);

  gate("shell.outline_panel", measureIterations("shell.outline_panel", 24,
                                                [&](int) {
                                                  type();
                                                  layoutPage();
                                                  askOutline();
                                                }),
       kShellOutlineBudgetMicros + kShellPageBudgetMicros + kShellEditBudgetMicros);

  // In the raw pane, deliberately: the bar's one expensive readout -- the
  // caret's line and column -- is shown where the note's *source* is on screen,
  // so measuring the bar in the default pane would measure the cheap half and
  // call it the bar. `shell.keystroke` below stays in the live pane, which is
  // the honest answer to what a keystroke costs *there*.
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Editor);
  gate("shell.status_bar", measureIterations("shell.status_bar", 24,
                                             [&](int) {
                                               type();
                                               askStatus();
                                             }),
       kShellStatusBudgetMicros + kShellEditBudgetMicros);
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Live);

  ui.fields.find.beginWith("paragraph");
  ui.find.open = true;
  gate("shell.find_scan", measureIterations("shell.find_scan", 24,
                                            [&](int) {
                                              type();
                                              askFind();
                                            }),
       kShellFindBudgetMicros + kShellEditBudgetMicros);
  micronotes::app::closeFindInNote(ui);
  ui.focus = micronotes::app::FocusArea::Editor;

  gate("shell.raw_pane_rewrap", measureIterations("shell.raw_pane_rewrap", 8,
                                                  [&](int) {
                                                    type();
                                                    askRawPane();
                                                  }),
       kShellRawBudgetMicros + kShellEditBudgetMicros);

  // And the whole thing, in the order the frame does it. This is the number
  // that would have caught both ninth-pass findings on the day they landed.
  gate("shell.keystroke", measureIterations("shell.keystroke", 24,
                                            [&](int) {
                                              type();
                                              layoutPage();
                                              askOutline();
                                              askStatus();
                                            }),
       kShellKeystrokeBudgetMicros);

  // The diagnosis behind the outline number: it is a borrow when the page has
  // already laid this revision out, and a whole-note block scan when it has
  // not. A change that reorders the frame turns every one of these into a scan
  // and moves no budget far enough to fail on a loaded machine.
  const auto counters = microcore::perf::captureCounters();
  const auto at = [&counters](microcore::perf::CounterId id) {
    return counters[static_cast<std::size_t>(id)];
  };
  using microcore::perf::CounterId;
  std::printf("%-40s %12llu borrowed %12llu scanned %12llu reused\n", "shell.outline_panel.blocks",
              static_cast<unsigned long long>(at(CounterId::RightPanelOutlineBlocksBorrowed)),
              static_cast<unsigned long long>(at(CounterId::RightPanelOutlineScans)),
              static_cast<unsigned long long>(at(CounterId::RightPanelOutlineReused)));
  if(at(CounterId::RightPanelOutlineScans) > at(CounterId::RightPanelOutlineBlocksBorrowed)) {
    std::cerr << "BUDGET FAILED: shell.outline_panel scanned the note more often than it borrowed "
                 "the page's partition -- the right panel is being asked before the content is "
                 "laid out\n";
    ok = false;
  }
  return ok;
}

}
