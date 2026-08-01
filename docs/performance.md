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

Measured 2026-08-01 on the `micronotes_perf` fixture (1000 notes, ~104 KB of
markdown in the heavy-document scenario).

| scope | calls | total | max |
|---|---|---|---|
| `library_index.refresh_changed_files` | 4 | 748 ms | **669 ms** |
| `fixture.app_state.open_select_and_list` | 1 | 66 ms | 66 ms |
| `fixture.large_library.create_1000_notes` | 1 | 60 ms | 60 ms |
| `fixture.markdown.parse_heavy_document` | 1 | 43 ms | 43 ms |
| `app_state.open_or_create_library` | 1 | 25 ms | 25 ms |
| `library_index.search` | 1 | 24 ms | 24 ms |

**`refresh_changed_files` dominates everything.** One call costs 669 ms, and it
runs on window focus. 29 statements are prepared across 4 refreshes. This is the
first thing to look at.

Not yet measured: the render path. `render.text_cache_*` and `frame.*` counters
are wired but the harness is headless, so they only report from a real session
under `MICROCORE_PERF_COUNTERS=1`. The text cache is known to flush *entirely*
at 4096 entries rather than evicting least-recently-used, which should show as a
large `render.text_cache_evictions` spike followed by a rasterization storm.
