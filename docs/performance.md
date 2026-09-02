# Performance

How to measure this app, and what the numbers currently say.

## The three instruments

Guessing at performance here is not necessary. All three live in
`src/core/perf/`, except the frame trace, which is app code (`src/app/`).

### Counters -- "how many times did this run?"

`PerformanceCounters.h`. One relaxed atomic add per event, cheap enough to stay
armed in release builds. This is the instrument that finds the real problems: a
sampling profiler tells you a function is hot, but only a counter tells you it
ran 4,000 times when it should have run once.

Counters are **deterministic**: the same workload produces byte-identical values
on every run and in every build type, because they count events, not cycles.
That is what makes them the evidence an optimisation actually worked, and the
timings merely corroboration.

Ids and wire names are declared once together through an X-macro. Keeping them
in two parallel lists -- an enum in the header and a positionally-indexed name
table in the .cpp -- lets an id be inserted without its name and silently
relabels every counter after it, attributing one subsystem's numbers to another.
The X-macro removes that failure mode instead of guarding against it.

Naming is `"<subsystem>.<event>"`. A name ending in a plural noun counts that
noun (bytes, blocks, lines); everything else counts calls or events.

### Scope timers -- "how long did it take, and who waits on it?"

`Perf.h` and `TraceChannel.h`. `perf::ScopeTimer timer("subsystem.operation");`
at the top of a scope. Results aggregate per label into **calls / total / self /
main / max**.

Two columns carry the weight:

- **self** is total minus the time charged to directly nested scopes. It is what
  the table ranks on, because a cheap outer scope that merely *contains* an
  expensive one must not outrank the expensive one. Ranking by total puts every
  caller above its own hotspot, which is the opposite of useful.
- **main** is the part of self spent on the thread `perf::markMainThread()`
  named -- the event loop. A background rescan costing 200 ms of CPU blocks
  nobody; a 30 ms frame drops the scroll. Without this column the two are the
  same row, and the tool built to find UI stalls points at the wrong one.

**The channels are off by default**, and that is the design, not an oversight. A
tracer that is always armed takes a process-wide mutex on every scope exit, so
it can only be put where it is cheap -- which is never where the problem is. A
disabled scope costs one predictable branch, copies no label and allocates
nothing, so a timer can sit on a per-block path.

Arm them from the environment:

```bash
MICROCORE_PERF_SUMMARY=1   ./build/bin/micronotes   # ranked table at exit
MICROCORE_PERF_TRACE=1     ./build/bin/micronotes   # one stderr line per scope
MICROCORE_PERF_TRACE_MIN_MS=1.0                     # filter the stream
MICROCORE_STARTUP_SUMMARY=1                         # the startup channel alone
```

Prefer the summary when hunting a hotspot: streaming writes and flushes inside
the measured region's parent, so it distorts exactly the number being read.

`perf::ScopeLabel` builds a `base(key=value,...)` label -- the note, the block
count -- and does the string work only when the channel is armed, so a label
that makes a row actionable costs nothing in a normal run.

### Frame trace -- "did the frame make it?"

`src/app/FrameTrace.h`. Frame time is the one number that decides whether the
app feels fast, and it was the one number nothing here measured: `frame.presents`
counts a 2 ms frame and a 40 ms frame identically, so a scroll that stutters and
one that does not produced the same instrumentation.

```bash
MICRONOTES_TRACE_FRAMES=1 ./build/bin/micronotes   # rolling summary per 120 frames
MICRONOTES_TRACE_FRAMES=2 ./build/bin/micronotes   # plus one line per frame
```

```
[frame] 60 frames | avg 2.78 ms | p50 1.89 | p95 2.30 | max 58.14 | 1 over 16.7 ms
        | blocks 18/10801 per frame | relaid 100.0 | runs 110
```

It reports **percentiles, not a mean**, because a mean hides exactly the frames
the user notices: a scroll averaging 6 ms with a p95 of 45 ms reads as janky and
the mean says it is fine. `blocks drawn/visited` is the other half -- how much of
each frame is spent deciding *not* to draw something.

`frame.draw_micros` and `frame.draws_over_budget` carry the same facts as plain
counters, so a session with no tracing armed still reports mean frame cost and
how many frames missed a vsync.

## Reading the numbers

The harness runs a fixed synthetic workload and prints both tables. It arms the
perf channel itself -- a benchmark whose instrumentation is off by default
measures nothing -- and exits non-zero when a scenario is over its budget.

```bash
tools/run-checks.sh perf        # Release build + harness -> /tmp/micronotes-perf.log
```

Use that lane rather than the default `build` tree. `build` is a **Debug** tree,
and an unoptimised harness reports timings several times the real ones, which is
worse than no number because it looks like a measurement. On this repo's fixture
the same scroll frame reads 4.5 ms in Debug and 0.67 ms in Release. Counters are
unaffected by build type, so a Debug run is still a valid counter reading.

For a real session rather than the fixture -- and the workload that is actually
slow is the one the user just did:

```bash
MICROCORE_PERF_COUNTERS=1 MICROCORE_PERF_SUMMARY=1 MICRONOTES_TRACE_FRAMES=1 \
  ./build-release/bin/micronotes
```

Everything is written to stderr at exit, by *every* exit path -- the event loop
ending, `--screenshot`, `--headless` -- because a dump wired into one of them is
silent for the others.

### Comparing against a baseline

```bash
tools/perf-compare.py [COMMIT]      # defaults to HEAD
ITERATIONS=10 tools/perf-compare.py main
```

Builds the comparison commit in a throwaway git worktree, runs both harnesses
several times, and prints what moved. Counters are compared exactly, because
every difference in them is real. Timings are compared against a k-sigma band
built from the per-iteration spread of both sides, and anything inside it is
dimmed as noise -- the same scenario has varied by more than 3x between runs on
a busy machine with identical counters throughout.

**A timing win with no counter movement behind it is usually the machine.**

## Adding instrumentation

A counter is two steps, and skipping the second fails the build:

1. Declare the id. Core-wide concepts go in
   `src/core/perf/PerformanceCounters.h`; micronotes-only ones go in
   `src/AppPerfCounters.h`. Both are `X(Id, "subsystem.event")` rows.
2. Increment it: `perf::addCounter(perf::CounterId::Id)`, or
   `perf::addCounter(perf::CounterId::Id, n)` to add more than one.

`architecture_every_perf_counter_has_a_producer` fails on any counter that
nothing increments. A counter reading zero forever is worse than an absent one:
a reader sees the missing row and concludes the code path did not run.

A timer is one line: `const perf::ScopeTimer timer("subsystem.operation");`.
Because the channel is off unless asked for, this is safe on a hot path -- and a
hot path is where it belongs. Write the counter for *what the work was*
alongside it; the timing says how long, and only the counter says whether it
should have happened at all.

`src/AppPerfCounters.h` is what keeps `src/core/perf/PerformanceCounters.h`
byte-identical between micronotes and microagenda -- the core header includes it
and concatenates the app list onto its own.

## Current findings

Counters first, because they are deterministic: the same workload produces
byte-identical counter values on every run. Wall-clock timings from this harness
are *not* reproducible under load -- the same scenario has varied by more than
3x between runs on a busy machine with identical counters throughout. Take
timings on an idle machine, and treat a timing change that is not corroborated
by a counter change as noise.

### Resolved: connection and statement churn in LibraryIndex

Measured 2026-08-01 on the `micronotes_perf` fixture (1000 notes):

| counter | before | after |
|---|---:|---:|
| `sqlite.connection_opens` | *unmeasured* | **1 per index** |
| `sqlite.statements_prepared` | 29 | **9** |
| `sqlite.exec_calls` | 18 | **14** |

Every method opened its own connection and recompiled its SQL. The index now
holds one connection for its lifetime and `microcore::persistence::SqliteDb`
caches statements on it.

Note the first row. `sqlite.connection_opens` previously read **zero**, not
because there were no connections but because `LibraryIndex` called
`sqlite3_open` directly and bypassed the instrumented wrapper. A counter that
reads zero because nothing increments it is indistinguishable from one that
reads zero because the code path did not run -- which is the exact failure mode
`architecture_every_perf_counter_has_a_producer` exists to prevent, and it did
not catch this one because the counter *did* have a producer, just not on this
path. Prefer routing through `SqliteDb` over opening a handle directly.

### Resolved: refresh did a write per window focus

`refreshChangedFiles` runs on every window focus. It used to take
`BEGIN IMMEDIATE` -- acquiring the write lock and forcing a WAL commit -- and
reload all 1000 `rows_` before it had established whether anything had changed.
It now scans first and only opens a transaction when there is something to
apply, which is confirmed by `sqlite.exec_calls` falling by two per no-op
refresh. `library.index_files_reread` distinguishes "walked the tree" from
"actually re-read a note", so the skip is measurable rather than assumed.

The tree walk also descended into the state directory -- the sqlite index, its
WAL, and every attachment -- and discarded the results by comparing path
prefixes, rebuilding `root_ / ".micronotes"` and two `std::string`s for *every*
entry in the tree. It now prunes that subtree. On a library with attachments
this is the largest part of the walk.

Reading `last_write_time` and `file_size` through the free functions made two
stat syscalls per file; going through the `directory_entry` the walk already
produced makes one, because it caches.

### Where the remaining time actually goes

Splitting the scope answered a question that guessing had got wrong:

| scope | calls | total | per call |
|---|---:|---:|---:|
| `library_index.refresh_changed_files` | 4 | 157 ms | — |
| `library_index.refresh.scan_tree` | 4 | 12.9 ms | 3.2 ms |
| `library_index.refresh.load_existing` | 4 | 2.5 ms | 0.6 ms |

The scan and the database read together are **15 ms of 157 ms**. The rest is the
*first* refresh reading and indexing 1000 notes from cold, which is real work,
not overhead. A steady-state refresh -- the one that runs on window focus -- now
costs about 4 ms for a 1000-note library, and the irreducible part of that is
the directory walk.

This is worth stating plainly because the earlier assumption was that the
database read dominated. It does not: it is 0.6 ms. Caching a tree signature to
skip that read would have optimised 0.4% of the scope, and the measurement is
the only reason that work did not get done.

`library_index_uses_one_connection_for_its_lifetime`,
`library_index_refresh_writes_nothing_when_nothing_changed`, and
`library_index_scan_does_not_descend_into_the_state_directory` guard all of it.

### Open: a scroll re-lays out the whole note, every frame

Found the day the frame trace and the layout counters were added, which is the
point of the section above: this was the slowest thing the app does and it was
invisible to every instrument the repo had. Every existing budget measured an
*edit*, so the harness could be green while the most common interaction --
moving the viewport over a note nobody is typing into -- cost more than typing.

Measured on a 235 KB note (10,801 blocks), Release, 60 frames:

```
[frame] 60 frames | avg 2.78 ms | p50 1.89 | p95 2.30 | max 58.14 | 1 over 16.7 ms
        | blocks 18/10801 per frame | relaid 100.0 | runs 110

[perf]      self ms      main ms     total ms       max ms       avg ms   calls  label
[perf]     111.526      111.526      129.309       52.078       2.1551      60  layout.update
[perf]      17.783       17.783       17.783        0.815       0.2964      60  layout.update.scan_blocks
[perf]      14.490       14.490       14.490        2.002       0.2415      60  page.draw
```

`layout.update` is **1.86 ms of self time in every frame** -- about 70% of the
frame -- and the counters say what it bought:

| counter | per frame | what it means |
|---|---:|---|
| `layout.unchanged_updates` | 59 of 60 | the update reproduced the previous layout exactly |
| `layout.source_bytes_copied` | 235,661 | the whole note copied into the layout's buffer |
| `layout.key_bytes_hashed` | 235,661 | the whole note hashed again for cache keys |
| `layout.blocks_scanned` | 10,801 | the block scanner re-partitioned the note |
| `layout.blocks_relaid` | 100 | ...and this is all that was actually rebuilt |
| `layout.flat_lines_built` | 12,002 | the caret/hit-test line table rebuilt from scratch |
| `layout.fold_queries` | 3,601 | the fold predicate asked about every foldable block |

Note the shape of it. The per-block layout cache is *working* -- 99% hit rate --
so a profile of the relayout itself looks fine. The cost is everything the
update does **before and around** the cache: the copy, the rescan, the hash of
every block's bytes to derive a key, and the flat-line rebuild. All four are
O(document) and none of them depends on the scroll offset, which is not an input
to the layout at all.

The draw has the same shape one layer up. `page.blocks_visited` is 10,801 per
frame against `page.blocks_drawn` of 18 -- 0.17% of the walk produces output --
and three further passes (`page.decoration_blocks_visited`,
`page.code_chrome_blocks_visited`, `page.fold_control_blocks_visited`) each walk
the whole block list again, for about 43,000 block visits a frame to draw 18 of
them.

None of this is fixed yet. What is fixed is that it is now *visible*, and that
`scroll.frame_relayout_median` in the harness fails a budget when a frame that
changed nothing costs more than 2 ms.

### Open: the status bar counts words every frame

`status.count_buffer` is 0.1 ms of every frame on the same note, and
`status.word_counts` tracks the frame count exactly. It recounts a 235 KB buffer
to render a number that changes only when the buffer does.
