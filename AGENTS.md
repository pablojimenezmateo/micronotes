# Agent Guide

First-stop operating guide for agents working in this repository.

## Quick Scan

- `micronotes` is a Linux-only Markdown notes app in C++20, CMake, SDL3, SQLite.
- Priority order: **speed, then correctness, then low CPU/memory**.
- `src/core/` is **vendored and shared with `microagenda`**. Read the rule below before touching it.
- Build with `cmake`, test with `ctest`, and prefer `tools/run-checks.sh` so output lands in a readable log.
- Performance work is measured, not guessed: `docs/performance.md` explains the counters and the harness.

## The Vendored Core Rule

`src/core/` is a byte-identical copy shared with `microagenda`. It holds the
markdown parser, editor, viewer, perf recorder and counters, sqlite wrapper,
path helpers, attachment service, and pane model.

**micronotes is canonical.** Make core changes here, then push them:

```bash
tools/sync-core.sh --push ../microagenda
```

and commit *both* repositories. `micronotes_core_manifest` (a ctest test) hashes
`src/core/` against `src/core/CORE.sha256` and fails if the two drift, so an
un-synced edit breaks the suite rather than diverging quietly.

Two invariants hold inside `src/core/`:

- It must not name a specific app. The one seam is `src/core/AppIdentity.h`,
  which reads `MICROCORE_APP_NAME` from the host `CMakeLists.txt`.
  `ArchitectureTests` enforces this.
- It lives in `namespace microcore`. `src/CoreAliases.h` aliases the subsystems
  into `namespace micronotes`, so app code still writes `platform::`, `perf::`,
  `markdown::` unqualified.

App-only code stays outside: `src/library/` (note library), `src/ui/AppState`,
`src/app/`.

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

Two complementary tools, both in `src/core/perf/`. See `docs/performance.md` for
the full description; the short version:

- **Counters** (`PerformanceCounters.h`) answer *how many times did this run*.
  One relaxed atomic add, cheap enough to leave armed in release.
- **Scope timers** (`Perf.h`) answer *how long did it take*. Aggregated by
  scope into calls / total / max.

Read them with the harness:

```bash
./build/bin/micronotes_perf
```

or from a real session:

```bash
MICROCORE_PERF_COUNTERS=1 ./build/bin/micronotes
```

Adding a counter is two steps, and skipping the second fails the build:

1. Declare it in `src/core/perf/PerformanceCounters.h` (core-wide concepts) or
   `src/AppPerfCounters.h` (micronotes-only), as `X(Id, "subsystem.event")`.
2. Increment it: `perf::addCounter(perf::CounterId::Id)`.

`ArchitectureTests` fails on a counter nothing increments. That is deliberate: a
counter that reads zero forever is worse than an absent one, because a missing
row is read as "this code path did not run" rather than "nobody wired this up".

When you add code on a hot path, add instrumentation with it. A blind spot found
later costs far more than a counter added up front.

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
- `src/app/Application.cpp` is a 3,000-line catch-all doing layout, input,
  rendering, and persistence. Do not grow it. New behaviour wants a named unit
  under `src/`, not another function in that file.

## Commits

One logical change per commit. Write the message so it explains *why* the change
was needed and what was wrong before -- the diff already shows what changed.
State measured numbers when the change is a performance one.
