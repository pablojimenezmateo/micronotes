#include "core/perf/TraceChannel.h"

#include "core/util/TransparentStringHash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace microcore::perf {
namespace {

using Clock = std::chrono::steady_clock;

// One slot per *live* armed channel. Two exist in the app (perf, startup); the
// cap keeps the thread-local active-scope table a fixed-size array rather than a
// per-thread allocation.
//
// Slots are reclaimed on destruction. A monotonic counter would be simpler but
// burns a slot per channel ever constructed, so a test that builds a few
// throwaway channels silently pushes the next one past the cap and disables it.
// Instrumentation that turns itself off is the worst failure mode a tracer has.
constexpr std::size_t kMaxChannels = 8;
constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
constexpr std::string_view kOverflowLabel = "<label-overflow>";

std::mutex& slotMutex() {
  static std::mutex mutex;
  return mutex;
}

std::array<bool, kMaxChannels>& slotsInUse() {
  static std::array<bool, kMaxChannels> inUse {};
  return inUse;
}

std::size_t acquireSlot() {
  const std::lock_guard<std::mutex> lock(slotMutex());
  std::array<bool, kMaxChannels>& inUse = slotsInUse();
  for(std::size_t i = 0; i < kMaxChannels; ++i) {
    if(inUse[i]) continue;
    inUse[i] = true;
    return i;
  }
  return kNoSlot;
}

void releaseSlot(std::size_t slot) {
  if(slot >= kMaxChannels) return;
  const std::lock_guard<std::mutex> lock(slotMutex());
  slotsInUse()[slot] = false;
}

// Innermost live scope per channel on this thread. A plain thread_local array of
// pointers: no allocation, and nesting depth stays per-thread, so a background
// scope cannot shift the indentation of a main-thread one mid-line or steal its
// self time.
thread_local std::array<TraceScope*, kMaxChannels> tActiveScope {};

// Set on the event-loop thread by markMainThread. A plain thread_local bool
// rather than a thread-id comparison: it is read on every scope exit.
thread_local bool tIsMainThread = false;
std::atomic<bool> gMainThreadMarked {false};

// "0", "false", "off", "no" all mean off, so MICROCORE_PERF_TRACE=0 does what
// it obviously means instead of arming the tracer.
bool falsey(std::string_view value) {
  return value == "0" || value == "false" || value == "off" || value == "no" ||
         value == "FALSE" || value == "OFF" || value == "NO";
}

bool envFlag(const char* name) {
  if(!name || !*name) return false;
  const char* value = std::getenv(name);
  if(!value || !*value) return false;
  return !falsey(value);
}

double envMinimumMs(const char* name) {
  if(!name || !*name) return 0.0;
  const char* value = std::getenv(name);
  if(!value || !*value) return 0.0;
  char* end = nullptr;
  const double parsed = std::strtod(value, &end);
  if(end == value || !std::isfinite(parsed)) return 0.0;
  return std::max(0.0, parsed);
}

double durationMs(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

}

void markMainThread() {
  tIsMainThread = true;
  gMainThreadMarked.store(true, std::memory_order_relaxed);
}

bool mainThreadKnown() {
  return gMainThreadMarked.load(std::memory_order_relaxed);
}

struct TraceChannel::Impl {
  mutable std::mutex mutex;
  Clock::time_point origin = Clock::now();
  // Keyed by an owned string: a label can be assembled per call (ScopeLabel),
  // so a view into the caller's buffer would dangle.
  // Looked up by string_view without materializing a std::string on every
  // record; the label is copied only when the entry is first created.
  std::unordered_map<std::string, Entry, microcore::util::TransparentStringHash, std::equal_to<>> entries;
};

TraceChannel::TraceChannel(const char* prefix, const char* streamEnv, const char* aggregateEnv,
                           const char* minMsEnv)
  : prefix_(prefix ? prefix : ""),
    stream_(envFlag(streamEnv)),
    aggregate_(envFlag(aggregateEnv)),
    minimumMs_(envMinimumMs(minMsEnv)) {
  if(!enabled()) return;
  acquireSlot();
}

TraceChannel::~TraceChannel() {
  // Channels are function-local statics, so this runs at exit. Disarm before
  // freeing: any TraceScope constructed after this point -- from a later static
  // destructor -- must take the disabled fast path rather than touch `impl_`.
  stream_.store(false, std::memory_order_relaxed);
  aggregate_.store(false, std::memory_order_relaxed);
  if(slot_ != kNoSlot) {
    tActiveScope[slot_] = nullptr;
    releaseSlot(slot_);
    slot_ = kNoSlot;
  }
  delete impl_;
  impl_ = nullptr;
}

void TraceChannel::acquireSlot() {
  if(slot_ != kNoSlot) return;
  slot_ = perf::acquireSlot();
  if(slot_ == kNoSlot) {
    // More live armed channels than the thread-local table can index. Degrade to
    // off rather than corrupting another channel's scope stack.
    stream_.store(false, std::memory_order_relaxed);
    aggregate_.store(false, std::memory_order_relaxed);
    std::fprintf(stderr, "[%s] trace channel disabled: more than %zu channels are live\n", prefix_,
                 kMaxChannels);
    return;
  }
  if(!impl_) impl_ = new Impl();
}

void TraceChannel::setAggregateEnabled(bool on) {
  aggregate_.store(on, std::memory_order_relaxed);
  if(on) acquireSlot();
}

void TraceChannel::reset() {
  if(!impl_) return;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->origin = Clock::now();
  }
  tActiveScope[slot_] = nullptr;
}

void TraceChannel::resetAggregate() {
  if(!impl_) return;
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->entries.clear();
}

TraceChannel::Clock::time_point TraceChannel::origin() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->origin;
}

void TraceChannel::record(std::string_view label, double totalMs, double selfMs, bool onMainThread) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  auto it = impl_->entries.find(label);
  if(it == impl_->entries.end()) {
    // Past the cap every new label folds into one bucket, so a session that
    // keeps inventing labels stops growing the table instead of leaking.
    if(impl_->entries.size() >= kMaxLabels) {
      label = kOverflowLabel;
      it = impl_->entries.find(label);
    }
    if(it == impl_->entries.end()) {
      it = impl_->entries.emplace(std::string(label), Entry {std::string(label)}).first;
    }
  }
  Entry& entry = it->second;
  ++entry.calls;
  entry.totalMs += totalMs;
  entry.selfMs += selfMs;
  if(onMainThread) entry.mainSelfMs += selfMs;
  entry.maxMs = std::max(entry.maxMs, totalMs);
}

void TraceChannel::recordSample(std::string_view label, double ms) {
  if(!aggregateEnabled() || !impl_) return;
  record(label, ms, ms, tIsMainThread);
}

std::vector<TraceChannel::Entry> TraceChannel::snapshot() const {
  std::vector<Entry> entries;
  if(!impl_) return entries;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    entries.reserve(impl_->entries.size());
    for(const auto& [label, entry] : impl_->entries) entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if(a.selfMs != b.selfMs) return a.selfMs > b.selfMs;
    return a.label < b.label;
  });
  return entries;
}

void TraceChannel::write(std::FILE* out) const {
  if(!out) return;
  const std::vector<Entry> entries = snapshot();
  if(entries.empty()) {
    std::fprintf(out, "[%s] summary: no scopes recorded\n", prefix_);
    std::fflush(out);
    return;
  }

  double selfTotal = 0.0;
  double mainTotal = 0.0;
  std::uint64_t callTotal = 0;
  for(const Entry& entry : entries) {
    selfTotal += entry.selfMs;
    mainTotal += entry.mainSelfMs;
    callTotal += entry.calls;
  }

  const bool mainKnown = mainThreadKnown();
  std::fprintf(out,
               "[%s] summary: %zu labels, %llu calls, %.2f ms self total, %.2f ms on the main "
               "thread (ranked by self ms)\n",
               prefix_, entries.size(), static_cast<unsigned long long>(callTotal), selfTotal,
               mainTotal);
  if(!mainKnown) {
    std::fprintf(out,
                 "[%s] note: no thread called markMainThread, so the main ms column is 0 "
                 "everywhere and says nothing\n",
                 prefix_);
  }
  std::fprintf(out, "[%s] %12s %12s %12s %12s %12s %10s  %s\n", prefix_, "self ms", "main ms",
               "total ms", "max ms", "avg ms", "calls", "label");
  for(const Entry& entry : entries) {
    const double avgMs = entry.calls ? entry.totalMs / static_cast<double>(entry.calls) : 0.0;
    std::fprintf(out, "[%s] %12.3f %12.3f %12.3f %12.3f %12.4f %10llu  %s\n", prefix_, entry.selfMs,
                 entry.mainSelfMs, entry.totalMs, entry.maxMs, avgMs,
                 static_cast<unsigned long long>(entry.calls), entry.label.c_str());
  }

  // A second ranking, by main-thread self time alone. The first table answers
  // "where does CPU go"; this one answers "what makes the app feel slow", and
  // they disagree often enough to be worth both. Suppressed when no thread was
  // marked, because the ordering would then be arbitrary.
  if(!mainKnown || mainTotal <= 0.0) {
    std::fflush(out);
    return;
  }
  std::vector<const Entry*> byMain;
  byMain.reserve(entries.size());
  for(const Entry& entry : entries) {
    if(entry.mainSelfMs > 0.0) byMain.push_back(&entry);
  }
  std::sort(byMain.begin(), byMain.end(), [](const Entry* a, const Entry* b) {
    if(a->mainSelfMs != b->mainSelfMs) return a->mainSelfMs > b->mainSelfMs;
    return a->label < b->label;
  });
  constexpr std::size_t kTopRows = 15;
  const std::size_t shown = std::min(kTopRows, byMain.size());
  std::fprintf(out, "[%s] top %zu of %zu main-thread scopes (what the user waits on):\n", prefix_,
               shown, byMain.size());
  for(std::size_t i = 0; i < shown; ++i) {
    const Entry& entry = *byMain[i];
    std::fprintf(out, "[%s] %12.3f main ms %10llu calls  %s\n", prefix_, entry.mainSelfMs,
                 static_cast<unsigned long long>(entry.calls), entry.label.c_str());
  }
  std::fflush(out);
}

void TraceChannel::dumpOnce() {
  if(!aggregateEnabled() || dumped_) return;
  dumped_ = true;
  write(stderr);
}

TraceScope::TraceScope(TraceChannel& channel, std::string_view label) {
  if(!channel.enabled()) return;

  // The label is copied only on the enabled path. These scopes sit on per-block
  // and per-run paths, so an unconditional copy here would be a heap allocation
  // per scope in production with tracing off -- the tracer would then be the
  // reason the app is slow.
  label_.assign(label);
  channel_ = &channel;
  slot_ = channel.slot_;
  parent_ = tActiveScope[slot_];
  tActiveScope[slot_] = this;
  depth_ = parent_ ? parent_->depth_ + 1 : 0;
  start_ = Clock::now();
}

TraceScope::~TraceScope() {
  if(!channel_) return;

  const Clock::time_point end = Clock::now();
  const double totalMs = durationMs(end - start_);
  tActiveScope[slot_] = parent_;
  if(parent_) parent_->childMs_ += totalMs;

  if(channel_->aggregateEnabled()) {
    channel_->record(label_, totalMs, std::max(0.0, totalMs - childMs_), tIsMainThread);
  }
  if(!channel_->streamEnabled() || totalMs < channel_->minimumMs()) return;

  const double elapsedMs = durationMs(end - channel_->origin());
  std::fprintf(stderr, "[%s] %8.2f ms in | %8.2f ms | %*s%s\n", channel_->prefix_, elapsedMs,
               totalMs, depth_ * 2, "", label_.c_str());
  std::fflush(stderr);
}

}
