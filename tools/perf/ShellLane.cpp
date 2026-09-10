#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "app/FindBar.h"
#include "app/MarkdownBlocks.h"
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
// shape. It drives a real `UiRuntime` -- a real editor, a real reading page,
// the real right-hand panel -- and stops short of the paint: no window, no
// textures, no present. Everything above the paint is where all three findings
// were.
//
// Order matters and is the app's. `drawApp` lays the content out and draws the
// right panel afterwards, because the outline borrows the block partition the
// reading page splices and `blocksAt` refuses to hand over one from a revision the
// layout has not reached. A lane that asked in the other order would measure
// the scan and call it the cost of the panel.
//
// The scenarios are separate so a regression is attributable to a surface, and
// `shell.keystroke` is their sum: the number a person typing actually pays.
static constexpr std::uint64_t kShellEditBudgetMicros = 200;
static constexpr std::uint64_t kShellOutlineBudgetMicros = 400;
// The status bar. It was 50 when the bar's whole job was a word count the
// editor already carried, then 250 when the segments added the caret's line and
// column -- a pass over the note from its first byte, memoised on the buffer's
// revision and the caret, which means a keystroke paid for it exactly once and
// that once was tens of microseconds.
//
// It is 40 now, because that pass is no longer from the first byte: the place
// the bar already had is an anchor, and the walk costs the distance the caret
// moved rather than the offset it sits at. The budget is back to being about
// the *bar* rather than about the size of the note, and a regression to
// scanning from the top puts it back over.
static constexpr std::uint64_t kShellStatusBudgetMicros = 40;
// The find bar's scan, which runs once per (buffer, needle, options) -- so
// once per keystroke while the bar is open, over the whole note. Its own
// scenario because it is the one surface whose cost the reader opts into, and
// because there was no lane for it at all when it was three separate scans: the
// page's, the raw pane's and the status line's, none of which the harness saw.
static constexpr std::uint64_t kShellFindBudgetMicros = 2000;
// The note page over a real face, which is the one part of a keystroke that
// shapes glyphs. Loose for the reason the font lane is loose: shaping is the
// scenario whose cost moves most with what else the machine is doing.
static constexpr std::uint64_t kShellPageBudgetMicros = 20000;
// The raw pane's soft wrap, which is now bounded by the edit rather than by the
// note. Two things got it here. It stopped *shaping* to find out where a line
// ends -- the pane is a monospaced grid, so a run of ASCII is as wide as it is
// long -- which took it from 70 ms to 0.7 ms; and then it stopped rewrapping
// the lines the edit did not reach, which took it from 494 us to 4 us and from
// 450 KB of rows a keystroke to none.
//
// So the budget is tight on purpose. At 3,000 us it was a ceiling on a number
// nobody was pleased with; at 100 it is the thing that fails when the wrap goes
// back to being whole-note, which is the only way this number moves by an order
// of magnitude.
static constexpr std::uint64_t kShellRawBudgetMicros = 100;
static constexpr std::uint64_t kShellKeystrokeBudgetMicros = 24000;
// The blocks md4c renders -- tables, raw HTML, footnote definitions -- with the
// hook actually wired, which for the rest of this lane it is not.
//
// It is its own scenario because it is the one part of the page whose shaping
// is *not* incremental: `doc::DocumentLayout` relays only the blocks that
// moved, but a complex block is one opaque height to it, so the block goes to
// md4c and through the line breaker again whenever it is asked. The memo in
// `ComplexRenderCache` is what makes that once per width rather than once per
// frame, and this is the number that says so: a change that drops the memo, or
// keys it on something that moves, multiplies this by the frame rate with
// nothing else in the harness noticing. Which is the gap this lane's own
// header describes -- a readout the instruments cannot see -- and it was open
// for the md4c path the whole time the path was rendered twice.
//
// Two scenarios: a keystroke reshapes the one block it landed in, and a change
// of *width* reshapes the blocks in view, which is what a resize and a panel
// toggle do. Both around 20 us and a few hundred allocations as measured.
//
// **What the budgets can and cannot catch.** Losing the memo means shaping
// each block once per frame instead of once per width -- roughly a doubling
// here, which is inside the run-to-run spread this machine has and so is not
// something a clock budget can gate. These numbers are therefore set to catch
// an order of magnitude, and the readable signal for the doubling is the
// **allocation count** printed beside them: the shaping allocates a few
// hundred and a hit allocates none. The structural claim itself is pinned by a
// test rather than by a number -- `render_layout_reuses_a_layout_at_the_same
// _width` in `tests/RenderLayoutTests.cpp`.
static constexpr std::uint64_t kShellTablesBudgetMicros = 1000;
static constexpr std::uint64_t kShellTablesResizeBudgetMicros = 1000;

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
  ui.readingPage.setHooks(std::move(hooks));

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
    ui.readingPage.beginFrame(frame);
    ui.readingPage.layout(text, ui.editor.text(), page);
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
  // call it the bar. `shell.keystroke` below stays in split view, which is
  // the honest answer to what a keystroke costs *there*.
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Editor);
  gate("shell.status_bar", measureIterations("shell.status_bar", 24,
                                             [&](int) {
                                               type();
                                               askStatus();
                                             }),
       kShellStatusBudgetMicros + kShellEditBudgetMicros);
  ui.state.editWorkspace().setPaneMode(micronotes::ui::PaneMode::Split);

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

  // The md4c blocks, with the hook wired to the real one. A separate runtime
  // rather than a mode of the one above: wiring the hook changes what every
  // scenario before this measures, and the fixture wants tables in it, which
  // the shared body has one of.
  {
    micronotes::app::UiRuntime tables;
    if(tables.state.openOrCreateLibrary(root)) {
      tables.state.selectNote(notes.front().id);
      std::string tableBody = "# Tables\n\n";
      for(int i = 0; i < 40; ++i) {
        tableBody += "| Name | Detail | Link |\n| --- | :---: | ---: |\n";
        for(int row = 0; row < 6; ++row) {
          tableBody += "| **bold " + std::to_string(row) + "** | *italic* and `code` | [a](b) |\n";
        }
        tableBody += "\nA paragraph between the tables.\n\n";
      }
      tables.editor.setText(tableBody);
      // Inside a table, not in the heading above them: a keystroke that lands
      // in an ordinary paragraph reshapes no complex block at all, and a lane
      // that typed there would report the memo's hit and call it the cost.
      const std::size_t inCell = tableBody.find("| **bold 3**", tableBody.size() / 2);
      tables.editor.moveCursor(inCell == std::string::npos ? tableBody.size() / 2 : inCell + 4);
      micronotes::app::PageViewHooks wired;
      wired.measureComplex = [&text, &tables](const micronotes::doc::SourceBlock& block,
                                              float width) {
        return micronotes::app::measureComplexBlock(text, tables, block, width);
      };
      tables.readingPage.setHooks(std::move(wired));
      const auto layoutTables = [&] {
        micronotes::app::PageFrame frame;
        frame.sourceRevision = tables.editor.revision();
        frame.editedSpan = tables.editor.lastChange();
        tables.readingPage.beginFrame(frame);
        tables.readingPage.layout(text, tables.editor.text(), page);
        micronotes::app::sweepComplexCache(tables, tables.readingPage.document().blocks(),
                                           tables.editor.text());
      };
      const auto layoutTablesAt = [&](float width) {
        micronotes::app::PageFrame frame;
        frame.sourceRevision = tables.editor.revision();
        frame.editedSpan = tables.editor.lastChange();
        tables.readingPage.beginFrame(frame);
        tables.readingPage.layout(text, tables.editor.text(), {0.0f, 0.0f, width, page.h});
        micronotes::app::sweepComplexCache(tables, tables.readingPage.document().blocks(),
                                           tables.editor.text());
      };
      layoutTables();
      gate("shell.tables_page", measureIterations("shell.tables_page", 8,
                                                  [&](int) {
                                                    tables.editor.insert("x");
                                                    layoutTables();
                                                  }),
           kShellTablesBudgetMicros);

      // Every complex block reshaped, because none of them is laid out at the
      // width being asked for. A *fresh* width each step, and both facts about
      // it were learned by getting them wrong:
      //
      //   * they have to be under `ui::pageWidthPx()`. The reading column is
      //     capped at that measure, so two window widths above the cap are the
      //     same column and relay nothing. That version read one microsecond
      //     with no allocations at all.
      //   * they have to be distinct. Two widths alternated are two the block
      //     cache and the render memo both already hold by the third step, so
      //     six of eight iterations measured a hit. That version read six
      //     microseconds, which is the memo working and not the shaping.
      gate("shell.tables_resize", measureIterations("shell.tables_resize", 8,
                                                    [&](int step) {
                                                      layoutTablesAt(520.0f -
                                                                     static_cast<float>(step) * 9.0f);
                                                    }),
           kShellTablesResizeBudgetMicros);
    }
  }

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
