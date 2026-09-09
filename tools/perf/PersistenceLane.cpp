#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "core/platform/DurableFile.h"
#include "ui/AppState.h"

#include <filesystem>
#include <iostream>
#include <string>


namespace micronotes::perfharness {

// The persistence lane.
//
// Autosave runs 1.2 s after the last keystroke and again every second while
// typing continues, and until this lane existed nothing measured it. Every
// other budget in this file is a layout or a paint, so a save could grow a
// whole-library tree walk and a whole-file read and the harness would stay
// green -- which is exactly what had happened.
//
// Timed on the **wall clock**, not on CLOCK_PROCESS_CPUTIME_ID like every
// scenario above. A durable write is two fsync barriers, and a barrier is time
// the process spends blocked rather than running: measured on CPU time the
// 1.1 ms this costs per save reads as about 60 us of work, which is a
// measurement of the wrong thing. The allocation counts beside the medians are
// still the machine-independent half.

// One autosave of a 200 KB note in a 1,000-note library. Loose, and wall-clock:
// the floor is whatever this filesystem charges for two fsyncs, which is not
// something the code can be held to. It is here to catch a save that acquires
// a second whole-library pass, not to police the disk.
static constexpr std::uint64_t kAutosaveBudgetMicros = 12000;
// Posting the recovery copy is what a *keystroke* pays. It is a copy into a
// mailbox and a notify, so it belongs under the keystroke budget with the
// layout -- and well under it, since the layout has to fit there too.
static constexpr std::uint64_t kRecoveryPostBudgetMicros = 400;

bool persistenceBudgets(const std::filesystem::path& root, const std::string& body) {
  std::cout << "\n=== what saving a note costs ===\n";
  micronotes::ui::AppState state;
  if(!state.openOrCreateLibrary(root)) {
    std::cerr << "persistence lane: could not open the fixture library\n";
    return false;
  }
  const auto notes = state.catalog().notes();
  if(notes.empty()) {
    std::cerr << "persistence lane: the fixture library has no notes\n";
    return false;
  }
  state.selectNote(notes.front().id);

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  std::string text = body;
  gate("save.autosave_note", measureWallIterations("save.autosave_note", 16, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)state.saveSelectedNote(text);
       }),
       kAutosaveBudgetMicros);

  // The UI thread's half of recovery: the queue absorbs the disk, so this is a
  // copy and a notify however busy the writer is.
  gate("save.recovery_post", measureWallIterations("save.recovery_post", 200, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)state.openNote().saveRecovery(text);
       }),
       kRecoveryPostBudgetMicros);
  (void)state.openNote().clearRecovery();

  // A save with nothing else open, so the number is the write and the index
  // update rather than the note list rebuild the two above also pay for.
  gate("save.durable_write_200kb", measureWallIterations("save.durable_write_200kb", 16, [&](int i) {
         text.push_back(static_cast<char>('a' + (i % 26)));
         (void)microcore::platform::writeFileDurably(root / ".micronotes" / "perf-write.tmp", text);
       }),
       kAutosaveBudgetMicros);
  std::filesystem::remove(root / ".micronotes" / "perf-write.tmp");
  return ok;
}

}
