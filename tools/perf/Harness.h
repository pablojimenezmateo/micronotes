#pragma once

#include "doc/Layout.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// The perf harness's instrument: the clocks, the allocation counter, the
// fixture text and the three printers. Every lane measures through this and
// nothing else, so a change to how a cost is taken is a change in one place.
//
// This was the top third of a 1,553-line `PerfMain.cpp` that also held all ten
// lanes. The lanes are independent -- each builds its own fixture, prints its
// own table and returns whether its budgets held -- so the file was ten
// subjects in a row, and `docs/performance.md` pointing a reader at "the shell
// lane" meant pointing at a line number.
namespace micronotes::perfharness {

// read-modify-write, and a background thread could never charge its churn to a
// measured iteration.
// Allocation counting, for the half of a measurement that does not move with
// the machine.
//
// Wall time on a shared developer box swings by a factor of three between runs,
// which is enough to hide a real regression and enough to invent one. An
// allocation count does not: the same binary over the same fixture produces the
// same number on a busy laptop and an idle one.
//
// Thread-local rather than atomic: the harness is single threaded, so this is a
// plain increment on the hottest path in the process rather than a
// read-modify-write, and a background thread could never charge its churn to a
// measured iteration.
//
// Declared here rather than hidden behind accessors because the measure
// templates below read them directly, and an accessor call on that path would
// be measured too.
extern thread_local std::uint64_t tAllocations;
extern thread_local std::uint64_t tAllocatedBytes;
// The biggest single allocation, which the count and the total together cannot
// answer: "one big buffer or a thousand small ones" is a different bug each
// way, and a scenario whose byte total is ten times its neighbour's at the same
// allocation count is one buffer being resized, not more work being done.
extern thread_local std::uint64_t tLargestAllocation;

// The fixture note every lane lays out, and the fixed-advance stand-in they
// measure it with.
std::string heavyMarkdown(int seed, int sections);
doc::Metrics stubMetrics();

void recordMicros(std::string_view name, std::uint64_t micros);
std::uint64_t cpuMicros();
std::uint64_t wallMicros();

void printSamples();
void printCounters();
void printPeakMemory();

// Somewhere for a result to go. Without it the optimiser is entitled to notice
// that nothing reads the answer and delete the work that produced it, which is
// a lane that measures nothing and says it is fast.
extern volatile std::size_t sink;

// One timed call. Every budget in this file used to spell the clock out for
// itself, five copies of the same duration_cast -- which is also why switching
// the clock was a five-place edit rather than a one-place one.
template <typename Fn>
std::uint64_t timeMicros(Fn&& body) {
  const std::uint64_t start = cpuMicros();
  body();
  return cpuMicros() - start;
}

struct Cost {
  std::uint64_t medianMicros = 0;
  std::uint64_t worstMicros = 0;
  double allocations = 0.0;
  double kilobytes = 0.0;
  std::uint64_t largestBytes = 0;
};


template <typename Fn>
inline Cost measureIterations(const char* name, int count, Fn&& iteration) {
  std::vector<std::uint64_t> samples;
  samples.reserve(static_cast<std::size_t>(count));
  // One untimed pass first. Without it the first scenario in the file absorbs
  // every buffer the layout grows once and keeps -- the block array, the source
  // ping-pong, the reuse map -- and reports them as the cost of a keystroke:
  // typing near the top read 189 KB per keystroke against 9 KB in the middle,
  // and the whole difference was the warm-up, not the position. Every scenario
  // here is meant to measure a steady state, and this is what puts it in one.
  iteration(0);
  const std::uint64_t allocationsBefore = tAllocations;
  const std::uint64_t bytesBefore = tAllocatedBytes;
  tLargestAllocation = 0;
  for(int i = 0; i < count; ++i) {
    samples.push_back(timeMicros([&] { iteration(i); }));
  }
  const double runs = static_cast<double>(count);
  Cost cost;
  cost.allocations = static_cast<double>(tAllocations - allocationsBefore) / runs;
  cost.kilobytes = static_cast<double>(tAllocatedBytes - bytesBefore) / runs / 1024.0;
  cost.largestBytes = tLargestAllocation;
  std::sort(samples.begin(), samples.end());
  cost.medianMicros = samples[samples.size() / 2];
  cost.worstMicros = samples.back();
  recordMicros(name, cost.medianMicros);
  std::printf("%-40s %7llu us median %7llu us worst %10.0f allocs %10.1f KB %8.1f KB max\n",
              name, static_cast<unsigned long long>(cost.medianMicros),
              static_cast<unsigned long long>(cost.worstMicros), cost.allocations, cost.kilobytes,
              static_cast<double>(cost.largestBytes) / 1024.0);
  return cost;
}


template <typename Fn>
inline Cost measureWallIterations(const char* name, int count, Fn&& iteration) {
  std::vector<std::uint64_t> samples;
  samples.reserve(static_cast<std::size_t>(count));
  iteration(0);
  const std::uint64_t allocationsBefore = tAllocations;
  const std::uint64_t bytesBefore = tAllocatedBytes;
  tLargestAllocation = 0;
  for(int i = 0; i < count; ++i) {
    const std::uint64_t start = wallMicros();
    iteration(i);
    samples.push_back(wallMicros() - start);
  }
  const double runs = static_cast<double>(count);
  Cost cost;
  cost.allocations = static_cast<double>(tAllocations - allocationsBefore) / runs;
  cost.kilobytes = static_cast<double>(tAllocatedBytes - bytesBefore) / runs / 1024.0;
  cost.largestBytes = tLargestAllocation;
  std::sort(samples.begin(), samples.end());
  cost.medianMicros = samples[samples.size() / 2];
  cost.worstMicros = samples.back();
  recordMicros(name, cost.medianMicros);
  std::printf("%-40s %7llu us median %7llu us worst %10.0f allocs %10.1f KB %8.1f KB max\n",
              name, static_cast<unsigned long long>(cost.medianMicros),
              static_cast<unsigned long long>(cost.worstMicros), cost.allocations, cost.kilobytes,
              static_cast<double>(cost.largestBytes) / 1024.0);
  return cost;
}

}
