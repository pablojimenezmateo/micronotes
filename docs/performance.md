# Performance

How to measure this app, and what the numbers currently say.

## The two instruments

Guessing at performance here is not necessary; both halves of the picture are
instrumented and live in `src/core/perf/`.

### Counters — "how many times did this run?"

`PerformanceCounters.h`. One relaxed atomic add per event, cheap enough to stay
armed in release builds. This is the instrument that finds the real problems: a
sampling profiler tells you a function is hot, but only a counter tells you it
ran 4,000 times when it should have run once.

Ids and wire names are declared once together through an X-macro. Keeping them
in two parallel lists — an enum in the header and a positionally-indexed name
table in the .cpp — lets an id be inserted without its name and silently
relabels every counter after it, attributing one subsystem's numbers to another.
The X-macro removes that failure mode instead of guarding against it.

Naming is `"<subsystem>.<event>"`. A name ending in a plural noun counts that
noun (bytes, blocks, lines); everything else counts calls or events.

### Scope timers — "how long did it take?"

`Perf.h`. `perf::ScopeTimer timer("subsystem.operation");` at the top of a
scope. Results are aggregated per scope into **calls / total / average / max**,
so a scope inside a loop reports one row rather than burying every other scope
under thousands.

The average and max columns matter: they separate *slow once* from *fast but
called far too often*, two problems with different fixes that a single total
conflates.

## Reading the numbers

The harness runs a fixed synthetic workload and prints both tables:

```bash
cmake --build build --target micronotes_perf
./build/bin/micronotes_perf
```

For a real session rather than the fixture, arm the shutdown dump:

```bash
MICROCORE_PERF_COUNTERS=1 ./build/bin/micronotes
```

Counters are written to stderr on exit.

## Adding instrumentation

Two steps, and skipping the second fails the build:

1. Declare the id. Core-wide concepts go in
   `src/core/perf/PerformanceCounters.h`; micronotes-only ones go in
   `src/AppPerfCounters.h`. Both are `X(Id, "subsystem.event")` rows.
2. Increment it: `perf::addCounter(perf::CounterId::Id)`, or
   `perf::addCounter(perf::CounterId::Id, n)` to add more than one.

`architecture_every_perf_counter_has_a_producer` fails on any counter that
nothing increments. A counter reading zero forever is worse than an absent one:
a reader sees the missing row and concludes the code path did not run.

`src/AppPerfCounters.h` is what keeps `src/core/perf/PerformanceCounters.h`
byte-identical between micronotes and microagenda — the core header includes it
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

### Open

Not yet measured: the render path. `render.text_cache_*` and `frame.*` counters
are wired but the harness is headless, so they only report from a real session
under `MICROCORE_PERF_COUNTERS=1`.
