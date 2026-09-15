#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/TraceChannel.h"
#include "core/markdown/MarkdownParser.h"
#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "ui/AppState.h"

#include <filesystem>
#include <iostream>
#include <string>

// The perf harness: build one fixture library, run every lane over it, and
// report. The lanes are in `tools/perf/`, one per file, and the instrument they
// all measure through is `perf/Harness.h`.
//
// Release only -- a Debug harness reports timings several times the real ones,
// which looks like a measurement and is not. See `docs/performance.md`.

using namespace micronotes::perfharness;

// What the whole run may peak at. The lanes settle at ~32 MB on this machine
// with a spread of 0.6% across runs, so the headroom here is for the allocator
// and the 1,000-note fixture rather than for drift: a lane that starts holding
// a second copy of the note or of the library moves this by more than the
// slack. See `docs/performance.md`, "The twelfth pass".
constexpr double kPeakMemoryBudgetMb = 48.0;

int main() {
  // The harness measures whether or not the developer remembered to export
  // anything: a benchmark whose instrumentation is off by default measures
  // nothing. The live app leaves both channels to the environment.
  microcore::perf::markMainThread();
  microcore::perf::traceChannel().setAggregateEnabled(true);

  const auto root = std::filesystem::temp_directory_path() / "micronotes-perf-fixture";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  {
    microcore::perf::ScopeTimer timer("fixture.large_library.create_1000_notes");
    for(int i = 0; i < 1000; ++i) {
      micronotes::library::NoteMetadata metadata;
      metadata.id = "perf-" + std::to_string(i);
      metadata.title = "Perf Note " + std::to_string(i);
      metadata.tags = {"perf", i % 2 == 0 ? "even" : "odd"};
      auto path = library.createNote(metadata, heavyMarkdown(i, 3));
      if(i % 4 == 0) library.moveNote(path, "work");
      else if(i % 4 == 1) library.moveNote(path, "ideas");
    }
  }

  micronotes::library::LibraryIndex index;
  index.open(root);
  index.refreshChangedFiles();
  index.refreshChangedFiles();
  {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "perf-updated";
    metadata.title = "Perf Updated";
    library.createNote(metadata, heavyMarkdown(2000, 8));
    index.refreshChangedFiles();
  }
  (void)index.search("searchable");

  {
    micronotes::ui::AppState state;
    microcore::perf::ScopeTimer timer("fixture.app_state.open_select_and_list");
    state.openOrCreateLibrary(root);
    state.selectFolder("work");
    (void)state.catalog().folders();
    (void)state.catalog().tags();
    auto notes = state.currentNotes();
    if(!notes.empty()) {
      state.selectNote(notes.front().id);
      (void)state.openNote().read();
    }
  }

  {
    microcore::markdown::MarkdownParser parser;
    microcore::perf::ScopeTimer timer("fixture.markdown.parse_heavy_document");
    const auto doc = parser.parse(heavyMarkdown(9999, 200));
    std::cout << "heavy_document.blocks: " << doc.blocks.size() << "\n";
  }

  std::string liveNote;
  bool withinBudget = layoutBudgets(&liveNote);
  withinBudget = editBudgets(liveNote) && withinBudget;
  withinBudget = scrollBudgets(liveNote) && withinBudget;
  withinBudget = selectionBudgets(liveNote) && withinBudget;
  withinBudget = interactionBudgets(liveNote) && withinBudget;
  withinBudget = fontBudgets(liveNote) && withinBudget;
  withinBudget = persistenceBudgets(root, liveNote) && withinBudget;
  withinBudget = shellBudgets(root, liveNote) && withinBudget;
  withinBudget = searchBudgets(root) && withinBudget;
  withinBudget = undoBudgets() && withinBudget;

  printSamples();
  printCounters();
  // The peak the whole run reached, gated like every other budget. Headroom
  // over the ~32 MB the lanes settle at, because the figure moves a little
  // with the allocator and with how the 1,000-note fixture happens to land --
  // but not enough headroom to hide a lane that starts holding a second copy
  // of the note or of the library. Raising it is a decision to be made out
  // loud, in `docs/performance.md`, the way the timing budgets are.
  withinBudget = gatePeakMemory(kPeakMemoryBudgetMb) && withinBudget;
  std::filesystem::remove_all(root);
  return withinBudget ? 0 : 1;
}
