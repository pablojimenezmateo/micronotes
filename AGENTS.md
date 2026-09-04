# Agent Guide

First-stop operating guide for agents working in this repository.

## Quick Scan

- `micronotes` is a Linux-only Markdown notes app in C++20, CMake, SDL3, SQLite.
- Priority order: **speed, then correctness, then low CPU/memory**.
- Known debt is in `docs/tech-debt.md`, numbered `TD-n`. Read it before deciding
  something is unaccounted for, and add to it rather than leaving a `TODO`.
- `src/core/` is the app-agnostic layer. Read the rule below before touching it.
- Build with `cmake`, test with `ctest`, and prefer `tools/run-checks.sh` so output lands in a readable log.
- Performance work is measured, not guessed: `docs/performance.md` explains the three instruments and the harness.

## The Core Rule

`src/core/` is the app-agnostic layer. It holds the markdown parser, editor,
viewer, perf counters and scope tracer, sqlite wrapper, path helpers, durable
file writes, attachment service, and pane model.

Two invariants hold inside it:

- It must not name a specific app. The one seam is `src/core/AppIdentity.h`,
  which reads `MICROCORE_APP_NAME` from `CMakeLists.txt`; every derived path is
  a compile-time constant off that macro. `ArchitectureTests` fails the build on
  a `micronotes` spelled out anywhere else under `src/core/`, because "agnostic"
  decays the moment it stops being checked.
- It lives in `namespace microcore`, which is what makes the layer a layer: a
  core header cannot reach app code without saying `micronotes::` out loud.
  `src/CoreAliases.h` aliases the subsystems into `namespace micronotes`, so app
  code still writes `platform::`, `perf::`, `markdown::` unqualified.

App-only code stays outside: `src/library/` (note library), `src/ui/AppState`,
`src/app/`.

**`src/core/` used to be a byte-identical vendored copy shared with a sibling
repo, kept in step by `tools/sync-core.sh` and a `CORE.sha256` manifest that a
ctest checked.** That is gone. The sibling never adopted the layout, so the
manifest was policing a copy that did not exist while adding a four-step dance
to every core edit. Do not reintroduce it: if a second app ever wants this code,
make it a library with a version, not a hashed copy.

Three targets are built from it. `micronotes_core` is everything above;
`micronotes_shell` is every surface that draws (`src/app/`, plus the four
`src/ui/` files they draw through); `micronotes` is `main.cpp` and a link line.
The test binary and the perf harness both link the **shell**, so a pane, a model
or a policy under `src/app/` is ordinary testable code -- it was not, until
recently, and the workarounds that left are still visible.

## Development Workflow

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Prefer the logging wrapper, which tees build+test output to a deterministic file
so results can be read back without rerunning:

```bash
tools/run-checks.sh tests   # -> /tmp/micronotes-tests.log
tools/run-checks.sh perf    # -> /tmp/micronotes-perf.log (Release harness)
tools/run-checks.sh asan    # -> /tmp/micronotes-asan.log
tools/run-checks.sh ubsan   # -> /tmp/micronotes-ubsan.log
tools/run-checks.sh tsan    # -> /tmp/micronotes-tsan.log
tools/run-checks.sh all     # all four in sequence
```

**After a run, READ the log instead of rebuilding and rerunning.** Each log
starts with a header naming the commit it came from.

The sanitizers are slow. Run them **once**, after a change is complete, not per
edit. `tests` is the inner loop.

Extra CMake arguments (a hand-pointed SQLite, for instance) go through
`CMAKE_EXTRA_ARGS` and apply to every configure the script performs.

## Performance Instrumentation

Three complementary instruments. `docs/performance.md` is the full description;
the short version:

- **Counters** (`src/core/perf/PerformanceCounters.h`) answer *how many times
  did this run*. One relaxed atomic add, armed in release. **Deterministic**:
  the same workload gives byte-identical values every run and in every build
  type, which is what makes them proof rather than evidence.
- **Scope timers** (`src/core/perf/Perf.h`) answer *how long did it take, and
  who waited on it*. Aggregated per label into calls / total / **self** / **main
  thread** / max. Ranked by self time, so a caller never outranks its own
  hotspot. **Off unless armed**, so a timer is safe on a per-block path.
- **Frame trace** (`src/app/FrameTrace.h`) answers *did the frame make it*.
  Rolling p50/p95/max over 120 frames plus what the frame drew. Percentiles, not
  a mean: a mean hides exactly the frames the user notices.

Read them with the harness, which must be **Release** -- a Debug harness reports
timings several times the real ones, which looks like a measurement and is not:

```bash
tools/run-checks.sh perf          # -> /tmp/micronotes-perf.log
tools/perf-compare.py main        # working tree vs a commit, in a worktree
tools/session-compare.sh main     # the same, through a REAL headless session:
                                  # interleaved runs, pixel cmp, counter diff
```

**The harness draws nothing.** It has a font lane -- real faces, real shaping,
through the same `app::documentMetrics` the live surface lays out with -- but no
window, no textures and no present, and nothing in it goes through `src/app/`'s
key handling, its shell surfaces or `AppState`'s writes to disk. So for anything
above the document layout a real session *is* the instrument, not a nicety. The
fifth pass in `docs/performance.md` found 1.1 ms of `fsync` on every keystroke
and a right-hand panel costing more per frame than the note; the seventh found
a note's pictures being re-resolved on disk once per layout. No harness lane
could see any of them.

Capture the pixels before and after as well: a paint or layout optimisation that
cannot be observed is safe, and `cmp` of two screenshots is the cheapest proof
there is.

Run a session, on screen or headless:

```bash
MICROCORE_PERF_COUNTERS=1 MICROCORE_PERF_SUMMARY=1 MICRONOTES_TRACE_FRAMES=1 \
  ./build-release/bin/micronotes

Xvfb :97 -screen 0 1600x1000x24 &
DISPLAY=:97 MICROCORE_PERF_COUNTERS=1 MICROCORE_PERF_SUMMARY=1 \
  ./build-release/bin/micronotes --library /path/to/library --select "Some Note" \
    --size 1600x1000 --pane live --panels sidebar,right --screenshot /tmp/shot.png
```

The headless form prints both tables and exits, so it is a command rather than a
sitting -- and two of them alternated between builds is the interleaved A/B the
run-to-run spread makes necessary. `tools/session-compare.sh` does exactly that
and diffs the counters and the pixels as well; `docs/performance.md` has the
full recipe behind it.

Adding a counter is two steps, and skipping the second fails the build:

1. Declare it in `src/core/perf/PerformanceCounters.h` (core-wide concepts) or
   `src/AppPerfCounters.h` (micronotes-only), as `X(Id, "subsystem.event")`.
2. Increment it: `perf::addCounter(perf::CounterId::Id)`.

`ArchitectureTests` fails on a counter nothing increments. That is deliberate: a
counter that reads zero forever is worse than an absent one, because a missing
row is read as "this code path did not run" rather than "nobody wired this up".

When you add code on a hot path, add instrumentation with it, and add the
*counter* as well as the timer. A timing says how long the work took; only a
counter says whether it should have happened at all. A blind spot found later
costs far more than a counter added up front. The scroll relayout in
`docs/performance.md` sat in the hottest path in the app, fully cached, passing
every budget, because nothing counted the work the cache did not cover -- and
when the counters went in it turned out to be 70% of every frame.

## Agent Best Practices

- Narrow the problem with fast inspection first: `rg`, `rg --files`, `sed -n`,
  `git show`. Prefer targeted reads over broad dumps.
- Treat performance work as measurable engineering. Take a harness reading
  before and after; do not claim a speedup you did not measure.
- Broad refactors are fine when they improve correctness or ownership. Backwards
  compatibility is not a constraint here.
- Fix a failing test or a real bug when you find one, even if it predates your
  change. Commit it separately from the work that uncovered it.
- Keep deterministic logic out of SDL event glue and paint code. Thin
  orchestration layers are easier to test.
- Prefer RAII, explicit ownership, and value semantics. Reach for inheritance
  only at a durable polymorphic boundary.
- Avoid hidden coupling through mutable global state. The perf tables are the
  deliberate exception, and they are process-wide by design.
- `src/app/Application.cpp` is a 2,700-line catch-all doing layout, input,
  rendering, and persistence. Do not grow it: `ArchitectureTests` holds it to a
  line budget that only ever goes down. New behaviour wants a named unit under
  `src/`, not another function in that file.
- Debt goes in `docs/tech-debt.md` as a numbered `TD-n`, with what it costs
  today and why it has not been paid. There are no `TODO` comments in this tree
  and it should stay that way -- a TODO is invisible to everyone who is not
  already reading that file.

## Commits

One logical change per commit. Write the message so it explains *why* the change
was needed and what was wrong before -- the diff already shows what changed.
State measured numbers when the change is a performance one.
