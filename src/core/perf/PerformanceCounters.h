#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include "AppPerfCounters.h"

namespace microcore::perf {

// Free-running process-wide event counters. One relaxed atomic add, cheap
// enough to leave armed in release builds -- which is the point. A scoped timer
// answers "how long did this take"; a counter answers "how many times did this
// actually run", and that is the question that finds the real problems. A
// sampling profiler shows a function is hot; only a counter shows it ran 4,000
// times a frame when it should have run once.
//
// The id and its wire name are declared once, together. Keeping them in two
// parallel lists -- an enum here and a positionally-indexed name table in the
// .cpp -- means inserting an id without inserting its name at the same position
// still compiles and silently relabels every counter after it, attributing one
// subsystem's numbers to another. The X-macro removes that failure mode rather
// than guarding against it.
//
// Naming: "<subsystem>.<event>". Counters ending in a plural noun count that
// noun (bytes, blocks, lines); everything else counts calls or events.
//
// Every counter declared here MUST be incremented somewhere in src/. A counter
// with no producer reads zero forever, which is worse than an absent counter: a
// reader sees the row missing from a dump and concludes the code path did not
// run. ArchitectureTests enforces this.
#define MICROCORE_PERF_COUNTERS(X)                                                     \
  /* --- markdown parsing ------------------------------------------------- */        \
  X(MarkdownParseCalls, "markdown.parse_calls")                                        \
  X(MarkdownParseBytes, "markdown.parse_bytes")                                        \
  X(MarkdownBlocksProduced, "markdown.blocks_produced")                                \
  X(MarkdownInlinesProduced, "markdown.inlines_produced")                              \
  X(MarkdownAutolinkRewrites, "markdown.autolink_rewrites")                            \
  /* --- editing ---------------------------------------------------------- */        \
  X(EditorInsertCalls, "editor.insert_calls")                                          \
  X(EditorEraseCalls, "editor.erase_calls")                                            \
  X(EditorUndoRecords, "editor.undo_records")                                          \
  X(EditorUndoRecordsCoalesced, "editor.undo_records_coalesced")                       \
  X(EditorUndoBytesRetained, "editor.undo_bytes_retained")                             \
  X(EditorUndoRecordsDropped, "editor.undo_records_dropped")                           \
  /* --- persistence ------------------------------------------------------ */        \
  X(SqliteStatementsPrepared, "sqlite.statements_prepared")                            \
  X(SqliteExecCalls, "sqlite.exec_calls")                                              \
  /* --- attachments ------------------------------------------------------ */        \
  X(AttachmentLinksCreated, "attachments.links_created")                               \
  X(AttachmentBytesCopied, "attachments.bytes_copied")

// Applications add their own ids through MICROCORE_APP_PERF_COUNTERS in
// src/AppPerfCounters.h, which lives outside src/core so this file stays
// byte-identical across every app that vendors the core.
#ifndef MICROCORE_APP_PERF_COUNTERS
#define MICROCORE_APP_PERF_COUNTERS(X)
#endif

#define MICROCORE_ALL_PERF_COUNTERS(X) MICROCORE_PERF_COUNTERS(X) MICROCORE_APP_PERF_COUNTERS(X)

enum class CounterId : std::size_t {
#define MICROCORE_PERF_COUNTER_ENUM(id, name) id,
  MICROCORE_ALL_PERF_COUNTERS(MICROCORE_PERF_COUNTER_ENUM)
#undef MICROCORE_PERF_COUNTER_ENUM
  Count,
};

inline constexpr std::size_t kCounterCount = static_cast<std::size_t>(CounterId::Count);

using CounterSnapshot = std::array<std::uint64_t, kCounterCount>;

void resetCounters();
void addCounter(CounterId id, std::uint64_t delta = 1);
std::uint64_t readCounter(CounterId id);
CounterSnapshot captureCounters();
std::string_view counterName(CounterId id);

// Counters that grew between two snapshots, sorted by name. This is how a perf
// scenario reports what its workload actually did.
std::vector<std::pair<std::string_view, std::uint64_t>> nonZeroCounterDelta(
    const CounterSnapshot& before,
    const CounterSnapshot& after);

// Write every non-zero counter to `out`, sorted by name. This is the live-app
// readout: without it the counters are only observable from the perf harness,
// so a real session's numbers stay unreachable.
void writeCounters(std::FILE* out);

// True when the MICROCORE_PERF_COUNTERS environment variable is set. Callers
// use it to arm a shutdown dump.
bool counterDumpRequested();

// Idempotent shutdown dump, gated on counterDumpRequested().
void dumpCountersOnce();

}
