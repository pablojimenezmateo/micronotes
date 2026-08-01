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
are *not* reproducible on a loaded machine -- the same scenario varied by more
than 3x between runs under load, with identical counters throughout. Take
timings on an idle machine, and treat a timing change that is not corroborated
by a counter change as noise.

Measured 2026-08-01 on the `micronotes_perf` fixture (1000 notes, ~104 KB of
markdown in the heavy-document scenario):

```
library.index_refresh_calls        4
library.search_calls               1
sqlite.statements_prepared        29
markdown.parse_calls               1
markdown.parse_bytes          104008
markdown.blocks_produced        1601
markdown.inlines_produced       3401
markdown.autolink_rewrites       200
```

`library_index.refresh_changed_files` is the dominant scope by a wide margin in
every run regardless of load, and it runs on window focus. That is the first
thing to look at. Unlike microagenda's store it does not re-prepare per
operation -- 29 statements across 4 refreshes -- so the cost is in the work
itself rather than in connection churn, and the counters to add before changing
anything are how many files it stats and how many it re-reads.

Not yet measured: the render path. `render.text_cache_*` and `frame.*` counters
are wired but the harness is headless, so they only report from a real session
under `MICROCORE_PERF_COUNTERS=1`. The text cache is now a bounded LRU, so
`render.text_cache_evictions` should climb steadily under scrolling rather than
spiking by thousands at once as the old flush-everything cache did.
