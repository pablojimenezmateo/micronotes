#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "library/LibraryIndex.h"

#include <filesystem>
#include <iostream>
#include <string>


namespace micronotes::perfharness {

// The search lane.
//
// Search had no budget at all, which made it the same blind spot autosave was
// before the eighth pass: it runs on *every keystroke* in the search box, over
// the whole library, and every instrument here measured a layout or a save.
//
// The two scenarios are the two different queries, and they fail differently.
// A query that matches goes through fts5 and then builds a snippet per result;
// a query that matches nothing falls through fts to the `LIKE` scan, which
// reads `lower(body)` of every note in the library and is the worst case by a
// wide margin. Typing a word one letter at a time passes through several of the
// second kind before reaching the first.
//
// Wall clock, like the persistence lane: this one goes to SQLite, so part of
// the cost is page-cache reads rather than work on this thread.
//
// The allocation column is the number to read first here. It is deterministic,
// and it is what says whether the query is *copying the library* to look at it:
// a result set is capped at 200 rows, so a per-row whole-body copy shows up as
// a fixed several-hundred allocations however small the query is.
static constexpr std::uint64_t kSearchHitBudgetMicros = 25000;
static constexpr std::uint64_t kSearchMissBudgetMicros = 60000;

bool searchBudgets(const std::filesystem::path& root) {
  std::cout << "\n=== what a search costs ===\n";
  micronotes::library::LibraryIndex index;
  if(!index.open(root)) {
    std::cerr << "search lane: could not open the fixture index\n";
    return false;
  }
  if(index.size() == 0) {
    std::cerr << "search lane: the fixture index is empty\n";
    return false;
  }

  bool ok = true;
  const auto gate = [&](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Every note in the fixture carries this word, so the result set is the
  // 200-row cap and every one of those rows has a snippet built for it.
  gate("search.query_hits_everything",
       measureWallIterations("search.query_hits_everything", 8, [&](int) {
         const auto results = index.search("searchable", micronotes::library::SearchScope::All);
         sink += results.size();
       }),
       kSearchHitBudgetMicros);

  // No note carries this, so fts5 returns nothing and the query falls through
  // to the LIKE scan over every row. The path a half-typed word takes.
  gate("search.query_matches_nothing",
       measureWallIterations("search.query_matches_nothing", 8, [&](int) {
         const auto results = index.search("zqxjvw", micronotes::library::SearchScope::All);
         sink += results.size();
       }),
       kSearchMissBudgetMicros);

  // Titles only. A narrower column and a much smaller result set, so this is
  // the control: it shares every part of the path except the bodies.
  gate("search.query_titles_only",
       measureWallIterations("search.query_titles_only", 8, [&](int) {
         const auto results = index.search("Perf", micronotes::library::SearchScope::Title);
         sink += results.size();
       }),
       kSearchHitBudgetMicros);
  return ok;
}

}
