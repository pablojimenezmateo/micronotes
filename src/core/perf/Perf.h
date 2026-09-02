#pragma once

#include "core/perf/TraceChannel.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace microcore::perf {

// The two scope-timing channels every app that vendors this core gets.
//
// They used to be one always-armed Recorder with a process-global mutex taken
// on every scope exit. That is affordable exactly once per library rescan and
// nowhere near affordable per block, per run or per frame -- so the timers only
// ever went where they were cheap, which is never where the problem is. Both
// channels are now off unless asked for, and a disabled scope costs one branch.
//
//   MICROCORE_PERF_TRACE=1       stream one stderr line per scope, indented by
//                                nesting depth, filtered by
//                                MICROCORE_PERF_TRACE_MIN_MS.
//   MICROCORE_PERF_SUMMARY=1     accumulate calls / total / self / max per label
//                                and print one ranked table at shutdown.
//
// Prefer the summary when hunting a hotspot: streaming writes and flushes
// inside the measured region's parent, which distorts the numbers being read.
TraceChannel& traceChannel();

// The same thing for the startup path, separately gated so a launch profile
// does not drown in per-frame scopes.
//
//   MICROCORE_STARTUP_TRACE=1    stream startup scopes.
//   MICROCORE_STARTUP_SUMMARY=1  rank them instead.
TraceChannel& startupChannel();

// Times its scope and folds the result into the perf channel. No-op when that
// channel is off, which is the default.
class ScopeTimer {
public:
  explicit ScopeTimer(std::string_view name) : scope_(traceChannel(), name) {}

private:
  TraceScope scope_;
};

// The same, on the startup channel.
class StartupScope {
public:
  explicit StartupScope(std::string_view name) : scope_(startupChannel(), name) {}

private:
  TraceScope scope_;
};

// Assembles a `Base(key=value,key=value)` label, doing the string work only when
// the channel is armed.
//
// A label carrying the note, block count or byte size it ran on is what makes a
// row actionable -- "which note was slow", not "some note was" -- but the
// concatenation is pure waste in a normal run. Every call site that wants one
// would otherwise hand-roll the same `if(enabled())` guard around a `+=` chain,
// and a missed guard is an allocation per call in production.
class ScopeLabel {
public:
  explicit ScopeLabel(std::string_view base) : ScopeLabel(traceChannel(), base) {}
  // Explicit-channel form, so the formatting and the off-path contract are
  // testable without mutating the process environment.
  ScopeLabel(TraceChannel& channel, std::string_view base);

  ScopeLabel(const ScopeLabel&) = delete;
  ScopeLabel& operator=(const ScopeLabel&) = delete;

  ScopeLabel& field(std::string_view key, std::string_view value);
  ScopeLabel& field(std::string_view key, long long value);

  // Empty when the channel is off: TraceScope ignores the label in that case,
  // so there is nothing to build.
  std::string_view view();

private:
  std::string text_;
  bool enabled_ = false;
  bool open_ = false;
};

// Write both instruments to stderr, once per process, for whichever of them the
// environment armed. Called from the app's shutdown path.
//
// A live session is where the interesting numbers are: the harness runs a
// synthetic workload, and the workload that is actually slow is the one the user
// just did.
void dumpAllOnce();

// Arrange for dumpAllOnce() to run at process exit.
//
// The app has several ways out -- the event loop ends, a --screenshot run
// returns after writing its file, --headless returns before a window is ever
// created -- and a dump wired into only one of them silently reports nothing
// for the others. Both channels are constructed here before the handler is
// registered, so their own destructors run after it rather than disarming them
// first. Idempotent.
void dumpAtExit();

}
