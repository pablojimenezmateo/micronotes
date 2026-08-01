#include "core/perf/PerformanceCounters.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace microcore::perf {
namespace {

using CounterArray = std::array<std::atomic<std::uint64_t>, kCounterCount>;

CounterArray& counters() {
  static CounterArray table;
  return table;
}

// Positionally aligned with CounterId by construction: both are expanded from
// the same list, so an id cannot exist without its name or drift out of
// position relative to it.
constexpr std::array<std::string_view, kCounterCount> kCounterNames = {
#define MICROCORE_PERF_COUNTER_NAME(id, name) std::string_view(name),
    MICROCORE_ALL_PERF_COUNTERS(MICROCORE_PERF_COUNTER_NAME)
#undef MICROCORE_PERF_COUNTER_NAME
};

std::size_t toIndex(CounterId id) {
  return std::min(static_cast<std::size_t>(id), kCounterCount - 1);
}

bool& dumpedFlag() {
  static bool dumped = false;
  return dumped;
}

}

void resetCounters() {
  for(std::atomic<std::uint64_t>& counter : counters()) {
    counter.store(0, std::memory_order_relaxed);
  }
}

void addCounter(CounterId id, std::uint64_t delta) {
  counters()[toIndex(id)].fetch_add(delta, std::memory_order_relaxed);
}

std::uint64_t readCounter(CounterId id) {
  return counters()[toIndex(id)].load(std::memory_order_relaxed);
}

CounterSnapshot captureCounters() {
  CounterSnapshot snapshot {};
  for(std::size_t i = 0; i < kCounterCount; ++i) {
    snapshot[i] = counters()[i].load(std::memory_order_relaxed);
  }
  return snapshot;
}

std::string_view counterName(CounterId id) {
  return kCounterNames[toIndex(id)];
}

std::vector<std::pair<std::string_view, std::uint64_t>> nonZeroCounterDelta(
    const CounterSnapshot& before,
    const CounterSnapshot& after) {
  std::vector<std::pair<std::string_view, std::uint64_t>> deltas;
  for(std::size_t i = 0; i < kCounterCount; ++i) {
    if(after[i] <= before[i]) continue;
    deltas.emplace_back(kCounterNames[i], after[i] - before[i]);
  }
  std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  return deltas;
}

void writeCounters(std::FILE* out) {
  if(!out) return;
  std::vector<std::pair<std::string_view, std::uint64_t>> rows;
  for(std::size_t i = 0; i < kCounterCount; ++i) {
    const auto value = counters()[i].load(std::memory_order_relaxed);
    if(value == 0) continue;
    rows.emplace_back(kCounterNames[i], value);
  }
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  std::fprintf(out, "=== performance counters (%zu non-zero of %zu) ===\n", rows.size(), kCounterCount);
  for(const auto& [name, value] : rows) {
    std::fprintf(out, "%-44.*s %12llu\n", static_cast<int>(name.size()), name.data(),
                 static_cast<unsigned long long>(value));
  }
}

bool counterDumpRequested() {
  const char* value = std::getenv("MICROCORE_PERF_COUNTERS");
  return value && *value && std::string_view(value) != "0";
}

void dumpCountersOnce() {
  if(!counterDumpRequested()) return;
  if(dumpedFlag()) return;
  dumpedFlag() = true;
  writeCounters(stderr);
}

}
