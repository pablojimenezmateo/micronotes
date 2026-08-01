#include "core/perf/Perf.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace microcore::perf {
namespace {

struct Aggregate {
  std::uint64_t calls = 0;
  std::uint64_t totalMicros = 0;
  std::uint64_t maxMicros = 0;
};

std::mutex& tableMutex() {
  static std::mutex mutex;
  return mutex;
}

// Keyed by the scope name. Names are string literals owned by the binary, so
// the map stores views into them rather than copies; only snapshot()
// materializes strings, and only for the caller that asked to read.
std::unordered_map<std::string_view, Aggregate>& table() {
  static std::unordered_map<std::string_view, Aggregate> entries;
  return entries;
}

}

Recorder& Recorder::instance() {
  static Recorder recorder;
  return recorder;
}

void Recorder::add(std::string_view name, std::uint64_t micros) {
  const std::lock_guard<std::mutex> lock(tableMutex());
  Aggregate& entry = table()[name];
  ++entry.calls;
  entry.totalMicros += micros;
  entry.maxMicros = std::max(entry.maxMicros, micros);
}

std::vector<Sample> Recorder::snapshot() const {
  std::vector<Sample> samples;
  {
    const std::lock_guard<std::mutex> lock(tableMutex());
    samples.reserve(table().size());
    for(const auto& [name, entry] : table()) {
      samples.push_back(Sample {std::string(name), entry.calls, entry.totalMicros, entry.maxMicros});
    }
  }
  std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
    if(a.totalMicros != b.totalMicros) return a.totalMicros > b.totalMicros;
    return a.name < b.name;
  });
  return samples;
}

void Recorder::clear() {
  const std::lock_guard<std::mutex> lock(tableMutex());
  table().clear();
}

ScopeTimer::ScopeTimer(std::string_view name)
  : name_(name), start_(std::chrono::steady_clock::now()) {}

ScopeTimer::~ScopeTimer() {
  const auto end = std::chrono::steady_clock::now();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
  Recorder::instance().add(name_, static_cast<std::uint64_t>(micros));
}

}
