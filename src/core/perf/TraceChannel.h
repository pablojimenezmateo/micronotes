#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace microcore::perf {

// Mark the calling thread as the one whose latency the user feels: the event
// loop / paint thread. Call once, early.
//
// Without it every ranked table is misleading in the same direction. A
// background library rescan that costs 200 ms of CPU but blocks nobody outranks
// a 30 ms frame that drops the scroll, and the table gives the reader no way to
// tell them apart -- so the tool built to find UI stalls points at the wrong
// row. Scopes record which side of that line they ran on.
void markMainThread();

// True once markMainThread() has run somewhere in this process. When it has not,
// the main-thread column is meaningless and the summary says so rather than
// printing zeros that read as "nothing ran on the main thread".
bool mainThreadKnown();

// One env-gated scope-timing sink.
//
// A channel has two independent output modes, both off by default:
//
//   stream     one stderr line per scope, indented by nesting depth. It is a
//              firehose: the write and flush happen inside the measured
//              region's parent, so streaming a hot inner scope perturbs exactly
//              the number being read.
//
//   aggregate  per-label calls / total / self / max, ranked by self time and
//              printed once at exit. This is the mode to use when hunting a
//              hotspot: no per-scope I/O, and the answer arrives sorted.
//
// Off by default is the whole point. A tracer that is always armed cannot be
// put on a per-block or per-run path -- the mutex it takes on every scope exit
// would cost more than the work it measures -- so the instrumentation ends up
// only where it is cheap, which is never where the problem is. Disabled, a
// scope costs one predictable branch and touches nothing else.
class TraceChannel {
public:
  struct Entry {
    std::string label;
    std::uint64_t calls = 0;
    // Wall time inside the scope, nested scopes on the same channel included.
    double totalMs = 0.0;
    // `totalMs` minus the time attributed to directly nested scopes. This is the
    // column that ranks hotspots: a cheap outer scope that merely contains an
    // expensive one must not outrank the expensive one.
    double selfMs = 0.0;
    // The part of `selfMs` that ran on the thread markMainThread() named. This
    // is the number that costs the user a frame.
    double mainSelfMs = 0.0;
    double maxMs = 0.0;
  };

  // Distinct labels retained before the rest fold into one overflow bucket, so
  // the table holds at most this many rows plus that bucket. Labels can embed a
  // note id or a block count, so an unbounded map is a leak proportional to
  // session length rather than to instrumentation sites.
  static constexpr std::size_t kMaxLabels = 4096;

  // `prefix` is the bracketed line tag ("perf", "startup"). `streamEnv` gates
  // the per-scope lines, `aggregateEnv` the summary; either may be null for a
  // channel without that mode. `minMsEnv`, when set, names a float env var that
  // suppresses stream lines below that many milliseconds.
  TraceChannel(const char* prefix, const char* streamEnv, const char* aggregateEnv,
               const char* minMsEnv);
  ~TraceChannel();

  TraceChannel(const TraceChannel&) = delete;
  TraceChannel& operator=(const TraceChannel&) = delete;

  bool enabled() const { return streamEnabled() || aggregateEnabled(); }
  bool streamEnabled() const { return stream_.load(std::memory_order_relaxed); }
  bool aggregateEnabled() const { return aggregate_.load(std::memory_order_relaxed); }
  double minimumMs() const { return minimumMs_; }

  // Arm the channel from code rather than from the environment. The perf
  // harness measures whether or not the developer remembered to export
  // anything, and a test needs a channel it controls; neither should have to
  // mutate the process environment to get one.
  void setAggregateEnabled(bool on);

  // Rebase the elapsed-time origin and clear this thread's nesting. No-op when
  // the channel is off.
  void reset();
  // Drop every accumulated label, keeping the channel armed. For a caller that
  // wants a table scoped to one phase rather than to the whole process.
  void resetAggregate();

  // Fold an already-measured duration into the same table, as a leaf
  // (self == total). For work that times itself in a shape a scope cannot wrap
  // -- a budget the harness computes as a median of samples, a cost that spans
  // two callbacks. No-op when aggregation is off.
  void recordSample(std::string_view label, double ms);

  // Ranked by descending self time. Safe to call from any thread.
  std::vector<Entry> snapshot() const;

  void write(std::FILE* out) const;
  // Idempotent per channel, so the shutdown path and a backstop can both call it.
  void dumpOnce();

private:
  friend class TraceScope;

  using Clock = std::chrono::steady_clock;

  void record(std::string_view label, double totalMs, double selfMs, bool onMainThread);
  Clock::time_point origin() const;
  void acquireSlot();

  const char* prefix_ = "";
  // Atomic because the destructor disarms them to tell *other* threads' live
  // scopes to take the fast path. Relaxed ordering is free on every target here
  // and enabled() is the hottest read in the tracer.
  std::atomic<bool> stream_ {false};
  std::atomic<bool> aggregate_ {false};
  bool dumped_ = false;
  double minimumMs_ = 0.0;
  // Index into the thread-local active-scope table, assigned on first arming.
  std::size_t slot_ = static_cast<std::size_t>(-1);

  struct Impl;
  Impl* impl_ = nullptr;
};

// RAII timer for one region on one channel. Costs a single predictable branch
// when its channel is off: the label is not copied and nothing is allocated.
class TraceScope {
public:
  TraceScope(TraceChannel& channel, std::string_view label);
  ~TraceScope();

  TraceScope(const TraceScope&) = delete;
  TraceScope& operator=(const TraceScope&) = delete;

private:
  TraceChannel* channel_ = nullptr;
  TraceScope* parent_ = nullptr;
  // Cached rather than read back through `channel_` on destruction. Channels are
  // function-local statics, so a scope alive at exit can outlive its channel's
  // destructor, which resets the slot to a sentinel -- indexing the thread-local
  // table with that would be an out-of-bounds write rather than a no-op.
  std::size_t slot_ = 0;
  std::string label_;
  std::chrono::steady_clock::time_point start_ {};
  double childMs_ = 0.0;
  int depth_ = 0;
};

}
