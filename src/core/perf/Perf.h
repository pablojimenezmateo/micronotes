#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace microcore::perf {

// Aggregated timing for one instrumented scope.
//
// This used to be one row per ScopeTimer destruction, appended to a vector that
// was never bounded and never aggregated: a long session grew it without limit,
// a scope inside a loop buried every other scope under thousands of near-empty
// rows, and reading the output meant summing by hand. Aggregating on the way in
// keeps the table proportional to the number of *instrumentation sites* rather
// than to how long the app has been running, and answers the question those
// rows were being summed to answer anyway.
struct Sample {
  std::string name;
  std::uint64_t calls = 0;
  std::uint64_t totalMicros = 0;
  std::uint64_t maxMicros = 0;
};

// Process-wide timing table. Timers can fire off the main thread, so the table
// is mutex-guarded. The lock is taken only on scope exit, and never on the
// counter path in PerformanceCounters.h -- that one has to stay cheap enough to
// leave armed in release builds.
class Recorder {
public:
  static Recorder& instance();

  void add(std::string_view name, std::uint64_t micros);

  // Sorted by total time descending: the order you actually want to read.
  std::vector<Sample> snapshot() const;
  void clear();
};

// Times its scope and folds the result into the Recorder.
//
// Takes a string_view, not a string: the old signature allocated on every
// construction, so measuring a scope perturbed the thing being measured. Names
// are compile-time literals at every call site, so nothing needs to own them.
class ScopeTimer {
public:
  explicit ScopeTimer(std::string_view name);
  ~ScopeTimer();

  ScopeTimer(const ScopeTimer&) = delete;
  ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
  std::string_view name_;
  std::chrono::steady_clock::time_point start_;
};

}
