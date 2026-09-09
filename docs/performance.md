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
[frame] 60 frames | work avg 1.68 ms | p50 0.22 | p95 0.34 | max 86.99
        | present avg 7.48 | 1 over 16.7 ms | blocks 11/11 per frame
        | relaid 21.9 | runs 503
```

It reports **percentiles, not a mean**, because a mean hides exactly the frames
the user notices: a scroll averaging 6 ms with a p95 of 45 ms reads as janky and
the mean says it is fine. `blocks drawn/visited` is the other half -- how much of
each frame is spent deciding *not* to draw something.

It also reports **work, not wall time**. `SDL_RenderPresent` blocks until the
next vsync, so the wall time of a frame is pinned to the refresh interval
whatever the app did: a frame that spent 0.2 ms building and one that spent 7 ms
both measure about 8.3 ms on a 120 Hz display. This instrument was doing exactly
that, and its steady "p50 8.46 ms" was the monitor rather than the app. The
percentiles now rank the part before the present; the wait is reported beside
them as `present avg`, and `ScopedFrame::markWorkDone()` is the split.

`frame.draw_micros` (work), `frame.present_micros` (the wait) and
`frame.draws_over_budget` carry the same facts as plain counters, so a session
with no tracing armed still reports them.

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

### What the harness does to a note

One section of the run walks a 200 KB note through what a person actually does
to one, and prints a line per interaction:

```
type.middle           16 us median    21 us worst    11 allocs     4.6 KB    4.2 KB max
caret.block_to_block   1 us median     2 us worst     3 allocs     0.2 KB    0.4 KB max
open.cold_layout    5035 us median  5445 us worst 28879 allocs  9123.1 KB  825.9 KB max
```

The scenarios are chosen so that each one can fail differently, and between them
they cover the cases where the incremental layout can be *wrong* as well as the
ones where it can be slow:

| scenario | what it stresses |
|---|---|
| `type.near_top` / `type.middle` / `type.at_end` | an edit is asymmetric -- near the top almost nothing above it carries over and everything below shifts, at the end the reverse. A reuse scheme only ever tested in the middle can be silently O(document) at one end. |
| `backspace.middle` | the other branch of every prefix/suffix comparison |
| `newline.split_and_join` | the block *partition* changes, so blocks below shift by an index as well as an offset |
| `caret.block_to_block` | the arrow keys: the source stands completely still and two blocks change which markers they show |
| `scroll.idle_frame` | the relayout a scroll must **not** do |
| `scroll.viewport_queries` | the work a scroll genuinely does -- `blockRange`, `offsetAt`, `blockAt` -- which has to stay proportional to the window, not the note |
| `open.cold_layout` | first paint, the one case here that is meant to be O(document) |
| `fold.toggle_heading` / `fold.idle_frame_after_toggle` | the toggle, and the idle frame after it that a stamped fold revision should make free |
| `resize.width_step` | every cached block invalidated at once, at a width never seen before |

Two things about the numbers on those lines.

**The clock is process CPU time, not wall clock.** Nothing in the harness waits
on IO or on another thread, so every wall-clock microsecond that is not CPU time
is time the scheduler gave to something else. That is not a small correction on
a shared machine: the same keystroke measured 292 us at load 1 and 898 us at
load 11, and neither figure was about the code. Under CPU time the same
comparison repeats to about a per cent, which is what makes a 5% change
readable. The clock costs a syscall rather than a vDSO read, a few hundred
nanoseconds against scenarios that run from 8 to 6,000 microseconds.

**Every scenario runs one untimed pass first.** The layout grows a set of
buffers once and keeps them, and without a warm-up whichever scenario runs first
pays for all of them and reports it as the cost of a keystroke. `type.near_top`
read 189 KB per keystroke against `type.middle`'s 9 KB, and almost all of that
difference was the warm-up rather than the position in the document. The
worst-case column was measuring the same thing -- iteration zero was the worst
iteration nearly everywhere: `scroll.idle_frame`'s worst went 143 us -> 9,
`caret.block_to_block` 164 -> 91, `type.near_top` 810 -> 423.

**The last column is the biggest single allocation in the scenario.** The count
and the total together cannot separate "one big buffer" from "a thousand small
ones", and those are different bugs: the first is a container resizing, the
second is a loop that should be reusing storage. It was added to answer one
question and answered it in a line -- see *typing near the top allocated a
different amount* below -- and it is cheap enough to leave on.

**`peak_rss` at the end of the run is the only number here about a ceiling
rather than a rate.** Read from `/proc/self/status`, so it covers the fixture as
well as the layout -- a thousand notes on disk and an SQLite index are in there
too -- which makes it a number to watch move between runs rather than to
attribute to any one part. It is what made the block cache's ceiling measurable
at all: the per-scenario columns say what a keystroke hands straight back, and
only this says what the process was still holding.

**Allocations sit beside every median, and they are still the number to trust
first.** An allocation count does not move at all between runs, in any build
type, on any load. It is also the number that answers two of the three
priorities directly -- a layout pass that grows a buffer from empty once per
visual line is burning CPU and memory whatever the clock says. The counting is a
thread-local `operator new` in `tools/PerfMain.cpp`, so it sees the harness's own
thread and nothing else. It is what caught a "faster" `pushLine` that allocated
more, and what proved two versions of the placement walk did byte-for-byte the
same work when their medians differed by 5%.

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
dimmed as noise.

**A timing win with no counter movement behind it is usually the machine.** The
CPU clock narrowed that band a long way but did not close it: `resize.width_step`
still swings 5% between runs of the same binary, because its median is taken over
eight iterations and the cache sweep lands in about three of them. When two
builds disagree there and the counters are identical, the counters are right.

**Interleave the runs.** Building both binaries and alternating them -- rather
than measuring all of A and then all of B -- costs nothing and removes the drift
that made a 12% rise appear in `scan_blocks`, a function neither side had
touched. An unchanged scope is the control: if it moves, the comparison is
measuring the machine. `tools/perf-compare.py` does *not* interleave; for a
change whose effect is smaller than the machine's spread, build the two
binaries by hand and alternate them, taking the **minimum** of each metric
across rounds. A minimum is the only statistic on a loaded box that means
anything: it is the run where the scheduler stayed out of the way.

### Running a real session without a screen

The harness measures shaping now (see "the harness could not see the font
path" below), but it draws nothing: no window, no textures, no present. For
first paint as the user sees it -- rasterizing, the shell surfaces, the panels
-- a real session is still the instrument. It can be had headlessly:

```bash
Xvfb :97 -screen 0 1600x1000x24 &
DISPLAY=:97 MICROCORE_PERF_COUNTERS=1 MICROCORE_PERF_SUMMARY=1 \
  ./build-release/bin/micronotes --library /path/to/library \
    --select "Some Note" --size 1600x1000 --pane live --panels sidebar,right \
    --screenshot /tmp/shot.png
```

`--screenshot` draws frames until the compositor has sized the window, writes
the window out and exits, so a session is a command rather than a sitting. It
prints the same two tables the harness does, over the real renderer and the
real faces.

`tools/session-compare.sh <baseline-ref>` is the two-sided form of this, and is
what to reach for when comparing a change against a commit: it builds both,
alternates the runs, `cmp`s the pixels and diffs the counters. Interleaving
rather than averaging, because the run-to-run spread on a real renderer is
wider than a real regression.

It is also the **rendering regression check**. A layout optimisation is only
safe if it cannot be observed, and the cheapest proof of that is the pixels:
capture the same note in `live`, `viewer` and `split` before and after and
`cmp` the files. Use the *same* library path for both captures -- the root
folder's name is drawn in the sidebar, so two copies of a library under
different names differ legitimately and tell you nothing.

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

`src/AppPerfCounters.h` is what keeps `src/core/perf/PerformanceCounters.h` free
of app-specific rows -- the core header includes it and concatenates the app
list onto its own.

## Current findings

Counters first, because they are deterministic: the same workload produces
byte-identical counter values on every run. The harness times itself on process
CPU time now, which brought its timings from unusable under load (a 3x spread
with identical counters throughout) to reproducible within about a per cent --
but a few per cent of spread is still there, so treat a timing change that no
counter corroborates as noise, and interleave the two builds rather than
measuring one after the other.

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

### Resolved: a scroll re-laid out the whole note, every frame

Found the day the frame trace and the layout counters were added, which is the
point of the section above: this was the slowest thing the app did and it was
invisible to every instrument the repo had before. Every existing budget
measured an *edit*, so the harness was green while the most common interaction
-- moving the viewport over a note nobody is typing into -- cost more than
typing did.

Measured on a 235 KB note (10,801 blocks), Release, 60 frames:

| | before | after |
|---|---:|---:|
| frame p50 | 1.89 ms | **0.59 ms** |
| frame p95 | 2.30 ms | **0.74 ms** |
| `layout.update` self, steady frame | 1.86 ms | **0.17 ms** |
| `page.draw` self, per frame | 0.24 ms | **0.09 ms** |
| `page.blocks_visited` per frame | 10,801 | **18** |
| `layout.source_bytes_copied` per frame | 235,661 | **0** |
| `layout.key_bytes_hashed` per frame | 235,661 | **0** |
| `layout.blocks_scanned` per frame | 10,801 | **0** |
| `layout.visual_rows` per frame (then `flat_lines_built`) | 12,002 | **0** |
| `status.count_buffer` per frame | 0.10 ms | **0** |

And in the harness, `scroll.frame_relayout_median` went 0.566 ms -> **0.011 ms**.

Three separate causes, all with the same shape.

**The layout rebuilt an answer it already had.** `layout.unchanged_updates` read
59 of 60: the live surface re-lays the note out once per frame whether or not
anything happened, and a scroll is by definition a frame where nothing did. The
per-block layout cache was working -- 99% hits -- so a profile of the relayout
looked healthy; the cost was the whole-document copy, rescan, per-block key hash
and flat-line rebuild *around* the cache, none of which depends on the scroll
offset. `DocumentLayout::update` now compares its inputs against what the
standing layout was built from and returns immediately when they match. The
comparison includes a `memcmp` of the whole note, which is still O(document) --
but it is one linear pass over bytes already in cache, about 12 µs against the
1.9 ms it replaces.

**The draw walked the document to paint the window.** Four separate full passes
over the block list -- text, decorations, code chrome, fold controls -- each
testing every block against the viewport, about 43,000 block visits per frame to
draw 18 of them. `DocumentLayout::blockRange` answers the same question with two
binary searches, since blocks tile the document in order. The range is widened
backwards to the head of a quote or callout run so a container that starts above
the viewport keeps its box.

**The status bar recounted the buffer.** `status.word_counts` tracked
`frame.presents` exactly. The comment above it claimed it ran "about once per
keystroke, because frames are event driven" -- a scroll, a hover and a window
focus each draw a frame and none touches the text. It is now memoised on
`MarkdownEditor::revision()`, a counter bumped by every mutation and nothing
else.

> The ninth pass took this further and both counters are gone with it: a memo on
> the revision cannot help on the one event that always moves the revision, so
> the count is carried across each edit instead and there is nothing left to
> memoise. See *the status bar recounted the whole note on every keystroke*.

The pattern in all three is worth naming, because it is the one the counters are
good at and a profiler is not: **the cache was fine and the work around the
cache was the cost**. Nothing was slow per call. Everything ran more often than
it needed to, and only a count says so.

`layout_update_does_nothing_when_nothing_changed`,
`layout_update_rebuilds_when_an_input_moves`,
`layout_update_rebuilds_when_a_fold_closes`,
`layout_block_range_covers_the_band_and_nothing_else` and
`editor_revision_moves_only_when_the_text_does` guard all of it, and the
`scroll` scenario fails its budget if a frame that changed nothing costs more
than 2 ms again.

### Resolved: the frame trace was measuring the monitor

Everything below was found by running a real session against a 400-note library
with a 460 KB note open, which is the reading the fixture harness cannot give:
its budgets all run against a fixed-advance stand-in for a font, and the largest
single cost in the app turned out to be real glyph shaping.

The first reading said `p50 8.46 ms`, steadily, whatever was on screen. That is
a 120 Hz refresh interval. `ScopedFrame` wrapped `SDL_RenderPresent`, which
blocks until the display is ready, so the instrument built to answer "did the
frame make it" was reporting the display's cadence and would have reported the
same number for a frame that did nothing. Splitting the sample at
`markWorkDone()` turned an 8.46 ms non-answer into a p50 of 0.99 ms of work with
7.5 ms of waiting, and only then was there anything to optimise.

Nothing else in this section would have been visible without that split, and
nothing in it would have been *attributable* without the second half of the same
change: `page.draw` was the only timed part of a frame, so a frame spent in the
sidebar or the chrome showed up as time that went nowhere. `shell.menu_bar`,
`shell.sidebar`, `shell.tab_strip`, `shell.breadcrumb`, `shell.content`,
`shell.right_panel`, `shell.status`, `shell.menu`, `shell.overlays` and
`shell.present` now cover it end to end.

The tables further down this file predate the shell overhaul and still carry
`shell.title_bar` and `shell.ribbon` rows. Those two surfaces no longer exist:
the icon rail and the self-drawn title bar became the menu bar and the
breadcrumb band. Read the old rows as the cost of the chrome that was there,
not as a budget anything still enforces.

### The shell overhaul cost, measured

The overhaul was a visual change rather than a performance one, but it added a
surface -- a menu bar that lays itself out on every frame -- so it was measured
rather than assumed. Interleaved headless sessions, before and after, twice
each on the same library and window (`1600x1000`, sidebar and right panel open,
live pane):

| | p50 | p95 |
| --- | --- | --- |
| before | 0.34 ms, 0.28 ms | 0.37 ms, 0.37 ms |
| after | 0.29 ms, 0.32 ms | 0.32 ms, 0.79 ms |

Inside the run-to-run spread either way, which is what the interleaving is for.
`frame.draw_micros` is *not* comparable across these runs and is the reason to
read the percentiles instead: it is dominated by the first frame's window map
and glyph-cache warm, and it moved 514k -> 233k -> 85k -> 51k across the four in
run order, which is a cache warming up rather than anything either build did.

The counters are the attributable part, and they are identical either side
except for three rows and the two new ones:

```
render.text_cache_queries    9180 -> 9480   (+5 per frame)
render.text_measure_calls    5399 -> 5945   (+9 per frame)
render.text_rasterizations    106 ->  113   (+7, once)
menu.bar_layouts                     60     (one per frame)
menu.bar_label_measures               6     (once, for six menus)
```

So the whole bar is five cached text draws and nine cached measures a frame,
plus seven rasterizations it pays once. `menu.bar_label_measures` staying at six
across sixty frames is the thing to watch: the labels are static and the table
is fixed, so a count that tracks `bar_layouts` means the memo's probe stopped
discriminating and every pointer motion is re-shaping six strings for nothing.

### Resolved: the sidebar rebuilt the library on every frame

`shell.sidebar` was 0.68 ms of a 0.99 ms frame -- most of a frame, to draw the
three dozen rows a panel is tall enough to show.

| counter | before | after |
|---|---:|---:|
| `sidebar.rows_built` per frame | 111 | **0** (1 on a change) |
| `tree.rows_built` per frame | 105 | **0** (1 on a change) |
| `shell.sidebar` self, steady frame | 0.68 ms | **0.10 ms** |

Three things, all the same shape as the scroll relayout above.

`TreeModel::rows` is O(library), not O(viewport): it relativises a path and
builds a `std::map` key for every note before it can place the first row, then
allocates a `TreeRow` -- a path and two strings -- per row. It ran every frame.

`AppState::folders()`, `tags()` and `allNotes()` returned their memos **by
value**, so each frame deep-copied the whole note list to read it.

Neither had anything to recompute. The row list is a pure function of the
library revision, what the tree has open, the query, the tag filter, which
bands are shut, the shortcut lists and the panel's size; only the scroll and
the panel origin move
on a normal frame, and both are an offset over a list already built.
`SidebarModel.cpp` -- a new unit, since `Application.cpp` is under a shrinking
budget -- holds the build and the memo in front of it, and `sidebar.rows_reused`
against `sidebar.rows_built` says which one ran.

### Resolved: the fold predicate, asked per block per frame

Listed as open above, and it was worse than the 0.05 ms estimated there: on the
460 KB note it was `layout.fold_queries` **2,801 per frame**, because the reuse
check has to resolve the folds to establish that they have not moved, and each
resolution builds a fold key and takes a map lookup on the note id.

The fix is not to make the predicate cheaper but to stop asking. `LayoutOptions`
now takes two optional identity stamps -- `sourceRevision` and `foldRevision` --
and when they are unchanged the reuse check skips both the memcmp of the whole
note and the per-block fold query. Zero means "cannot say", so a caller with no
revision to offer (the harness, the tests) gets exactly the old behaviour.

| counter | before | after |
|---|---:|---:|
| `layout.fold_queries` per frame | 2,801 | **0** |
| `layout.update` self, steady frame | 0.18 ms | **~0.00 ms** |

`layout_stamped_reuse_asks_the_fold_predicate_nothing`,
`layout_a_moved_fold_stamp_re_resolves_the_folds` and
`layout_an_unstamped_caller_still_compares_bytes` guard it. The stamps are a
promise, so the one place that breaks it is handled explicitly: the caret-unwind
loop in `PageView::layout` expands a fold mid-pass and withdraws the stamp for
the pass that follows.

### Resolved: opening a large note shaped every word of it

With the frame path quiet, the remaining cost was the one-off: `layout.update`
took **147 ms** to open a 460 KB note of unique prose, which is a third of a
second of dead window between clicking a note and seeing it.

`render.text_measure_calls` said what it was: **154,096** calls for 1,311 blocks
laid out. `TTF_GetStringSize` shapes the run -- resolves each glyph, applies
kerning -- for about a microsecond a word, and 120 ms of the 147 ms was inside
it. No timing said this; a profile would have shown a hot function in SDL_ttf
and left the reader to guess whether it was called too often.

Two fixes, in that order.

**The same words are measured over and over.** Word frequency in prose is
Zipfian, every space is the same space, and a scroll re-measures what the last
frame measured. `ui::TextMeasureCache` is a direct-mapped table of 64-bit hash
to width in front of `TextRenderer::width`. Direct-mapped rather than LRU
because a miss here costs one shaping call, not a texture upload, so the
simplest policy buys the whole win with one array and no allocation.

**Spaces were measured twice each.** `Flow` held whitespace back to decide
whether a line breaks before or after it, measured the pending run to test the
fit, and then measured every token of it *again* inside the flush that emitted
it. Roughly half a document's tokens are whitespace.

| | before | after |
|---|---:|---:|
| `layout.update` opening 460 KB | 147 ms | **65 ms** |
| `render.text_measure_calls` | 154,096 | **106,224** |
| `render.text_measure_cache_hits` | *unmeasured* | **88,710** (84%) |
| frame work p50, steady | 0.48 ms | **0.22 ms** |

The cache hit rate is the number to watch: it is measured on genuinely unique
prose (a 400-word common vocabulary over a full dictionary tail), and a
collapse in it means either the cache is too small or something has started
measuring strings nobody measures twice.

### Resolved: an edit re-hashed and re-probed the whole document

A cache key here is a pure function of a block's *bytes and scan fields* -- never
of where in the buffer it sits. That is what makes an identical block share one
layout wherever it appears, and it also means an edit cannot invalidate a key it
did not touch, however far it pushed that block down the document.

The placement loop was not using this. Every update hashed every block's bytes
and did a hash-map probe per block to rediscover the layout it had resolved to
last frame: on a 200 KB, 9,612-block note, 200 KB of hashing and 9,612 random
probes to relay out one changed block. Split out, it was ~70% of a keystroke.

`mapUnchangedBlocks` now pairs the new scan against the standing one -- by index
across the untouched prefix, and by distance-from-the-end across the untouched
suffix, which is what makes the edit's shift irrelevant. A paired block keeps its
key, and with it the layout pointer, so neither the hash nor the probe happens:

| counter, 158 updates over the fixture | before | after |
|---|---:|---:|
| `layout.key_bytes_hashed` | 7,784,850 | **415,134** |
| `layout.cache_hits` (map probes) | 354,531 | **8,542** |
| `layout.blocks_key_reused` | -- | 345,989 of 365,256 walked |
| `layout.blocks_relaid` | 10,725 | 10,725 |

`blocks_relaid` not moving is the point: the same layouts get built, they are
just found without re-deriving how to find them. Interleaved A/B on one binary,
min of eight pairs: **0.555 -> 0.379 ms** per keystroke.

The identity case falls out of the same machinery. When the source has not moved
at all -- which is what an arrow key does -- the map is the identity and not one
key is rebuilt. Moving the caret used to rehash the entire document to discover
that one block had started showing its markers.

`mapUnchangedBlocks` itself is gone now -- see *the rescan re-derived every block*
below. Pairing the two lists was still an O(document) comparison; the splice
that replaced it produces the same two numbers, `head` and `tail`, without
deriving the new list in the first place. What survives from this section is the
property it established, which everything since rests on: **a cache key is a
function of a block's bytes and fields and never of its offsets.**

### Resolved: a keystroke allocated 2.6 MB

`scanBlocks` returned its vector by value, so `blocks_ = scanBlocks(source)`
freed the old block list and grew a new one from scratch on every keystroke --
and its reserve estimate was 2x low, so it grew twice on the way. Prose runs
nearer one block per 20 bytes than the one per 48 the estimate assumed.

`scanBlocksInto` writes into a vector the caller owns and keeps, so the list is
grown once for the life of the document. The layout's other per-update buffers
(`placed_`, `liveKeys_`, `flags_`, the source) now ping-pong through spares
rather than being moved from, which leaves them with capacity to reuse.

| per keystroke, 200 KB note | before | after |
|---|---:|---:|
| bytes allocated | 2,597 KB | **9.3 KB** |
| allocations | 42 | **23** |

### Resolved: laying out a block allocated six times

Opening a 460 KB note is the largest single cost in the app because it all lands
in one frame, and with the measure cache in place it is no longer font shaping:
of 145,504 measurements, 143,301 are cache hits. What was left was allocator
traffic -- six allocations and 2.9 KB per block, for a *staging* buffer that is
thrown away as soon as the block is flowed.

Four things, all the same shape -- a buffer that lives for one block and is grown
from empty for every block:

- `Flow`'s run and whitespace vectors are constructed per block, so they never
  kept their capacity. They are the layout's now, and are grown once per
  document. (I first tried making `pushLine` hand a right-sized buffer to each
  line instead of moving its own across; on its own that was a *pessimisation*,
  because `Flow`'s lifetime meant the buffer it kept was thrown away anyway. The
  allocation counter is what said so -- the wall clock could not.)
- The token vector was grown from empty for every block. It is reserved from the
  span's length now: prose runs about one token per three bytes.
- The token *groups* are staged in a buffer the layout owns, whose inner vectors
  keep their token storage between blocks. This meant filling the groups in
  order rather than splicing the opening fence in afterwards, which the fenced
  code path was doing.
- A block with no inline markup -- most of them, 3,502 of 4,202 in real prose --
  no longer allocates and zero-fills an attribute slot per content byte to
  conclude that every byte is plain.

| laying out a 200 KB note from scratch | before | after |
|---|---:|---:|
| allocations | 58,778 | **41,697** |
| bytes allocated | 27.7 MB | **14.4 MB** |

### Resolved: the harness was measuring the scheduler

Every budget timed itself with `steady_clock`, which counts the microseconds the
process spent descheduled as though they were work. On this machine that was
most of the number once anything else was running: `type.middle` read 293 us at
load 1, 864 us at load 11, and 326 us at load 1 again -- three measurements of
one unchanged binary. Worse, the drift was slow enough to survive a whole run,
so measuring all of A and then all of B produced a confident 12% "regression" in
`scan_blocks`, a function neither side touched.

Two fixes, both in `tools/PerfMain.cpp`:

- **`CLOCK_PROCESS_CPUTIME_ID`.** Nothing here waits on IO or another thread, so
  CPU time is the operation's cost. Repeatability went from a factor of three to
  about a per cent: across an interleaved A/B of twelve scenarios, `scan_blocks`
  -- untouched by the change under test -- read 24.898 ms and 24.562 ms. That is
  the control that makes the rest of the table mean something.
- **One untimed warm-up pass per scenario**, described under *Reading the
  numbers* above. It was hiding a 20x allocation difference between two
  scenarios that do the same thing in different places.

The clock was spelled out inline at five call sites, which is also why changing
it was a five-place edit; there is one `timeMicros` now.

### Resolved: the row index was a list, and both readers walked it

`flatLines_` was a materialised table of visual rows -- `{block, line, top}` per
row, 13,536 of them on a 460 KB note -- rebuilt from scratch on every update.
Then both of its readers walked it **from the front**: `offsetAt`, which answers
"what did this click hit", and `flatLineForOffset`, which answers "what row is
the caret on". So every click and every up-arrow cost a pass over every row in
the document, and every keystroke cost building the table to be walked.

None of it needed to exist. A row is a block plus a line index, and the layout
already holds the blocks and their lines; the only thing the table added was the
mapping from a flat row number to that pair. That mapping is a **prefix sum**:
one integer per block, filled by the placement walk that was already running.
Row tops are non-decreasing across it -- blocks tile the document in order and
lines tile their block -- so every query is a binary search, and a block folded
to zero rows repeats its predecessor's value, which `upper_bound` steps over in
one move.

`selectionRects` was the third reader and had the same shape for a different
reason: it walked every row in the note to find the handful the selection covers.
Blocks are ordered by source offset, so it starts at the block containing the
selection's start and stops at its end. `blockAt` was a fourth: a linear scan
over the block list, directly above a `blockRange` that was already binary
searching the same array on the same argument.

| interleaved A/B, min of four runs | before | after |
|---|---:|---:|
| `scroll.viewport_queries` | 8 us | **1 us** |
| `layout.update.place_blocks` + `.flat_lines`, 261 calls | 68.4 ms | **38.7 ms** |
| `layout.row_index_probes` per query | 13,536 | **13.5** |
| bytes allocated per cold layout | 14,414 KB | **14,039 KB** |

This change and the counter one below landed together, so the interaction
scenarios in *Where a frame goes now* carry both. Isolated, this half took
`place_blocks` + `flat_lines` from 68.4 ms to 56.2 ms and
`caret.block_to_block` from 134 us to 110 us.

The probe counter is the guard: a linear scan and a binary search return the same
answer, so nothing about the *results* would show a regression back to a walk.
`layout_row_lookups_binary_search_the_index` asserts against it, and
`layout_row_motion_steps_over_collapsed_blocks` covers the case the prefix sum
gets wrong if it is written carelessly -- a folded block contributes no rows, so
the index is full of runs of repeated values that one move has to cross. Both
were mutation-tested: reverting the search to a walk fails the first, and an
`upper_bound` written as a `lower_bound` fails the second.

### Resolved: the counters were a measurable part of the loop they measured

The placement walk incremented four counters per block. A counter add is one
relaxed atomic read-modify-write on a process-wide cacheline -- nothing at all
on a call per frame, and about 25 cycles per block when there are ten thousand
blocks. Counted into locals and posted once per update instead, with the totals
byte-for-byte identical:

| isolated A/B, min of three runs | per-block | batched |
|---|---:|---:|
| `layout.update.place_blocks`, 261 calls | 56.2 ms | **48.3 ms** |
| `type.middle` | 267 us | **237 us** |
| `caret.block_to_block` | 110 us | **83 us** |

That is 11% of a keystroke that was the instrument rather than the work. The
lesson is not "fewer counters" -- the counters are why any of the rest of this
page exists -- it is that a counter on a per-item path wants to be a local that
posts once, the way `blocks_walked` and `key_bytes_hashed` already were.

### Resolved: the cache sweep re-found every block it had not evicted

The sweep that bounds the block cache ended with a loop re-pointing all 9,612
entries of the placement at the map: one hash probe per block, which is exactly
the cost the key-reuse path exists to avoid. It was never needed.
`unordered_map` is node-based, and erasing an element invalidates pointers into
*that element* only -- every key in `liveKeys_` survives the sweep by
construction, so every pointer in `placed_` was still valid. The sweep also
counted its evictions one atomic at a time (up to 19,000 of them) and copied
`liveKeys_` to sort it, on the one frame it was already the slowest thing in.

`resize.width_step`'s worst iteration went 17.9 ms -> **14.0 ms**. Its median
moved the other way by about 5%, and the counters say that is measurement: the
two builds report identical `blocks_relaid`, `cache_hits`, `cache_evictions` and
`visual_rows`, so they do identically much work. The sweep has a timer of its own
now (`layout.update.evict_cache`, 7.2 ms across a harness run) so it stops
hiding inside the update's total.

Leaning on node stability deserved a test rather than a comment, so
`layout_survives_a_cache_sweep_that_erases_most_of_the_map` forces a sweep and
then reads the entire layout back -- the read that would fault on a freed node.
It runs in the ASan and UBSan lanes.

### Resolved: `setMetrics` left the placement pointing into a freed cache

Not a performance finding; found while reading the cache. `setMetrics` drops
every cached block layout, and every entry in `placed_` is a pointer *into* that
cache. Its one caller re-lays out on the next line, so nothing reads the
placement in between -- but "safe as long as nobody asks" is a use-after-free
waiting for a second caller, and dropping the placement costs nothing on a path
that has already thrown the whole document's layout away. The layout now answers
as an empty document until the next update, which every query was already
written to handle.

### Resolved: the placement rebuilt the whole document on every update

`layout.update.place_blocks` was 64.6 ms over 270 calls and the largest single
cost in a keystroke. It walked every block in the note, every time: computed the
block's flags, compared them against the previous generation's, copied a pointer,
accumulated a running `top` and appended to the row prefix sum -- ten thousand
iterations to move one paragraph two pixels down.

The walk was there because the placement was *rebuilt*. Four parallel arrays --
`placed_`, `flags_`, `liveKeys_`, `lineStart_` -- were filled front to back into
fresh buffers each update, with the previous generation kept in a second set of
four so the loop could read keys out of it. Rebuilding is O(document) by
construction, and no amount of making the loop body cheaper changes that.

It is patched in place now, and the shape of the patch is the interesting part:

- **The blocks whose *entry* can have moved are collected as a few ranges, not
  one span.** The contributors are countable, and each one is local: the blocks
  the edit re-derived (plus one either side, because `groupFirst`/`groupLast` are
  decided by a neighbour's kind), the block the caret left and the one it
  arrived at, the same for the raw block, and the run a fold change hid or
  revealed. A caret at the top of a note and an edit at the bottom are two
  ranges of one block each. One interval covering both would have been the whole
  document, which is how this kind of fix quietly fails to be a fix.
- **A range is walked absolutely; the gaps between ranges are crossed by a
  delta.** After recomputing a range, the difference between the running `top`
  and the one already stored at the next block is what that range moved
  everything below it by. When the difference is zero -- a keystroke inside a
  paragraph that does not rewrap -- the rest of the document is *already*
  correct and the walk stops. When it is not, the tail is a pass of one float
  add and one integer add per block, with no flags, no hash and no random probe
  into a map with an entry per block.
- **An index shift is a memmove rather than a rebuild.** When the block count
  changes, the four arrays' tails slide to meet the new indexing before the
  patch runs, so "which old block is this" stays the identity and the entries
  after the edit keep their key and their cached layout.

That deleted the three spare arrays as well -- 230 KB of vector on a 10k-block
note, and a generation of state to keep consistent. The `reuse_` map that said
which old block each new one came from survived this change and did not survive
the next one: once the splice below produced the carried-over ends as two
integers, "did this block keep its entry" became
`i < head || i >= count - tail`, and the `size_t`-per-block array it replaced was
itself an O(document) write per keystroke.

This was the first change of the pass, so "before" here is the session's
baseline. Interleaved A/B, CPU time, min of four:

| | before | after |
|---|---:|---:|
| `layout.update.place_blocks` self, 270 calls | 64.608 ms | **40.933 ms** |
| `layout.blocks_walked` | 2,595,028 | **202,593** |
| `caret.block_to_block` | 116 us | **4 us** |
| `type.middle` | 345 us | 233 us |
| `fold.toggle_heading` | 291 us | 151 us |
| `layout.update.evict_cache` (the control) | 9.999 ms | 10.314 ms |

Nearly all of the 202,593 blocks still walked are the 21 *rebuilds* in the run
-- the first open of a note, and the resizes. The 249 patches walk about nothing.

**One bug worth recording, because it is the shape of bug this design invites.**
The cache key mixed in `index == 0` directly, and the old reuse check guarded
that separately with `(from == 0) == first`. Making the mapping the identity
removed the guard and left the key: a block that kept its bytes but stopped
being the *first* block in the document -- inserting a new paragraph above it --
went on sharing the first block's layout, which has no space above it. It showed
up as a 14-pixel height difference at block 1 after 307 steps of the random
edit walk. The fix is that "first block" is now a `Flags` bit like
`trailingLine`, its symmetric opposite, and `layoutBlock` reads the flag rather
than re-deriving it from the index: the flags *are* the record of what a cached
layout depends on beyond the block's own bytes, so anything that belongs in the
key belongs in them.

### Resolved: the rescan re-derived every block to find the one that changed

`layout.update.scan_blocks` was 41.1 ms over 182 calls -- 226 us a keystroke, and
once the placement patch landed it was about 80% of what a keystroke cost.
`scanBlocks` re-derived all 9,612 blocks of a 200 KB note, and then
`mapUnchangedBlocks` compared the result against the previous list to discover
that all but one of them were identical.

The page used to say a fix needed "a restart point whose scanner state is
reproducible (fence open, list depth, ordinals)". Reading the scanner settled
that: **there is no scanner state.** Every block is decided from its own first
byte forward -- a paragraph absorbs the lines *after* it, a fence closes on a
later line, a table is a table because of the row under it, `listDepth` comes
from the block's own indentation and `ordinal` from its own marker text. No
branch looks at a byte before the block it is building. So a scan resumed at any
block boundary produces exactly the blocks a scan from the top produces there,
and the restart point needs no state at all -- only to be a boundary.

That turns the rescan into a splice with two ends to establish:

- **Where to resume.** A block's classification can depend on the line that
  follows it, so the finest thing an edit can be said to have touched is the line
  holding its first changed byte, not the byte. Resume two blocks above that
  line. One block back is what the argument needs -- the block whose extent the
  changed line decides -- and the second is margin that costs two cache probes.
  Removing the margin entirely fails the random edit walk in 39 steps; removing
  only the second block passes it, which is the difference between a proof and a
  guess.
- **Where to stop.** At the first block boundary the scan reaches that (a) lies
  inside the bytes the edit left alone at the end of the buffer, and (b) the
  previous scan also started a block at. Together those say the rest of the new
  buffer is byte-identical to the rest of the old one *from a shared boundary* --
  and a stateless scan over identical bytes from a shared boundary produces
  identical blocks. So the rest of the list is the list already in hand, moved by
  however many bytes the edit added or removed.

`scanBlocksFrom` takes the byte count as a number and the boundary test as a
predicate, in that order, so the predicate -- which is a binary search -- is
asked only where it can say yes. An edit at the top reaches the untouched tail
after one block and stops; an edit at the end never reaches it and scans the
handful of blocks between the resume point and the end of the buffer. Both are
O(edit).

`mapUnchangedBlocks` and `sameBlockShape` are gone: the splice knows what carried
over by construction, so there is nothing left to discover by comparison, and
`layout.update.map_blocks` -- 9.9 ms over 170 calls -- is not a scope any more.

Measured against the build immediately before it -- so this table is the splice
alone, not the splice plus the placement patch. Interleaved, CPU time, min of six:

| | before | after |
|---|---:|---:|
| `layout.update.scan_blocks` self, 182 calls | 41.092 ms | **8.925 ms** |
| blocks re-derived per rescan (`layout.blocks_rescanned`) | 9,612 | **3** |
| `layout.update.map_blocks` self | 9.911 ms | **gone** |
| `type.middle` | 277 us | **31 us** |
| `type.at_end` | 272 us | **14 us** |
| `newline.split_and_join` | 585 us | **111 us** |
| `caret.block_to_block` | 5 us | **1 us** |
| `layout.update.place_blocks` (a control) | 49.308 ms | 52.116 ms |
| `layout.block.flow` (a control) | 55.096 ms | 57.666 ms |

Every control in that run reads 3-5% *higher* on the after side, which is the
after side's slot in the round being the noisier one -- so the after column is
if anything pessimistic. `layout.blocks_relaid` and `layout.visual_rows` are
identical across it, as they have to be.

The safety argument is a property, so it is tested as one.
`blockscan_resuming_at_every_boundary_matches_a_full_scan` resumes at *every*
boundary of five documents -- one with a table, a fence, a callout, indented
code, an unclosed fence, no trailing newline -- and asserts the resumed blocks
equal the full scan's field for field. A construct added later that looks
backwards fails there, rather than by leaving a stale block on screen after an
edit three paragraphs above it.

### Resolved: the layout copied the whole note on every keystroke

The layout keeps its own copy of the buffer, because every run of every cached
block points into it and the caller's buffer is not the layout's to hold.
Keeping that copy current was `spareSource_.assign(source)` plus a swap: 200 KB
memcpy'd per typed character, and a second 200 KB buffer to hold it in.

The prefix/suffix window is computed against the standing `source_` now rather
than against a copy of it, so it is available *before* anything is written, and
the copy becomes `source_.replace(prefix, removed, ..., added)` -- the bytes that
moved, plus a memmove of whatever follows them.

| against the build before it, min of six | before | after |
|---|---:|---:|
| `layout.source_bytes_copied` | 37,294,442 | **2,459,143** |
| `layout.source_bytes_moved` | -- | 18,544,745 |
| `type.middle` | 31 us | **27 us** |
| `type.at_end` | 14 us | **10 us** |
| `type.near_top` allocation | 41.1 KB | **7.7 KB** |
| `type.at_end` allocation | 0.8 KB | 0.8 KB |

The 18.5 MB that reappears as `source_bytes_moved` is the memmove an insertion
drags the rest of the buffer through -- averaging 29 KB an edit. That half is
still O(document) and is named as such in the counter, because a flat buffer has
no way around it.

The second buffer is gone with it, and so is the ping-pong: `spareSource_` was
one of two 200 KB strings alternating, and each of them reallocated when it was
handed a string longer than the capacity it had been sized for.

### Resolved: typing near the top allocated a different amount from typing in the middle

An open item on this page said `type.near_top` allocated 42.2 KB per keystroke
against `type.middle`'s 8.9 KB at the same 21 allocations, and that the way to
find out why was "a largest-single-allocation column beside the existing two,
five lines in `tools/PerfMain.cpp`". That was right, and the answer took one run:

```
type.near_top    335 us median   21 allocs   42.2 KB    400.2 KB max
type.middle      338 us median   21 allocs    8.9 KB      2.4 KB max
type.at_end      335 us median   12 allocs   19.6 KB    300.4 KB max
```

400 KB and 300 KB are geometric growth steps of a ~200 KB `std::string`. It was
never about the position of the caret: `type.near_top` runs first, so it is where
the layout's second copy of the buffer crossed its capacity and doubled, and the
doubling then left the later scenarios with headroom. The asymmetry was scenario
order meeting `std::string` growth.

Both allocations belonged to the ping-ponged source copy described above, and
with that copy gone `type.near_top` allocates 7.7 KB against `type.middle`'s
7.8 KB, largest 2.4 KB on both. The lesson is not about strings: an averaged
byte total hid a one-off behind twenty-four iterations, and no amount of staring
at the count would have found it.

### Resolved: the block cache kept three generations of the document

`cache_` was swept when it exceeded `blocks * 3 + 256` entries, on the theory
that three generations buy back the case where a key returns -- an undo, a
retype, a window dragged back to a width it just left. This page said the trade
was real and that nobody had measured it. Measured, interleaved, on the 200 KB
fixture:

| | `blocks * 3 + 256` | `blocks + 256` |
|---|---:|---:|
| `peak_rss` | 55.1 MB | **28.7 MB** |
| `resize.width_step` worst frame | 24.1 ms | **11.1 ms** |
| `resize.width_step` median | 10.9 ms | 10.2 ms |
| `resize.width_step` largest single allocation | 328.5 KB | **3.0 KB** |
| `layout.blocks_relaid` | 112,363 | 112,363 |
| `layout.cache_sweeps` | 1 | 9 |
| `layout.cache_evictions` | 26,864 | 48,228 |
| `layout.update.evict_cache` total | 11.3 ms | 24.5 ms |
| every `type.*`, `fold.*`, `caret.*`, `open.*` median | -- | unchanged |

`blocks_relaid` is *identical*. Not one extra block was laid out at a third of
the ceiling, which is the whole case for the larger one: on this workload no key
ever came back.

The rest of the table is one number in two shapes. Fourteen width steps at three
generations are **one** sweep freeing 26,864 layouts, and that single sweep *is*
the worst frame of a window drag. At one generation they are nine sweeps of about
5,400 each: 24 ms of `free` in total rather than 11, and no frame over 11 ms.
Total work up, spike down -- and the spike is the part anyone sees. Trading the
total for the spike is the right way round on a 60 Hz surface, and it is a trade
worth naming rather than hiding, because the total is what a throughput benchmark
would have reported.

`layout.cache_sweeps` was added alongside the evictions for exactly that reason:
evictions alone cannot tell one 24 ms sweep from nine small ones.

The number to watch if the smaller ceiling is ever wrong is
`layout.blocks_relaid` per update -- that is the counter a lost cache hit would
move, and it did not move at all.

### Resolved: the inline scan allocated two buffers per block, one of them for nothing

`layoutBlock` asked `scanInlines` for the inline spans of every block it laid
out, and `scanInlines` allocated two vectors per call: the span list it returns,
and a byte mask over the text saying which bytes structural scanning has claimed.
The span list is only allocated when there are spans -- and `layout.plain_blocks`
says four blocks in five have no markup at all -- but **the mask was allocated
and zero-filled by every one of them**, for a scan that then found nothing to
put in it.

Both are now caller-owned, which is the pattern the rest of this file already
follows (`scanBlocksInto`, `resolveFolds`, `flowRuns_`, `flowGroups_`): an
`InlineScratch` holding the two buffers, held by `DocumentLayout` as a member,
`assign`ed rather than reallocated per block. `scanInlines` remains as the
one-off form, implemented on top of it. The per-byte attribute table
`layoutBlock` fills from the spans got the same treatment, which is why `Attr`
moved into `Layout.h` beside `Token` -- for the same reason and with the same
comment: the layout owns the buffer, the code that fills it does not.

| interleaved A/B, min of five | before | after |
|---|---:|---:|
| `open.cold_layout` allocations | 41,696 | **32,092** |
| `open.cold_layout` bytes | 13,981 KB | **10,889 KB** |
| `resize.width_step` allocations | 36,457 | **28,047** |
| `resize.width_step` bytes | 9,359 KB | **6,648 KB** |
| `type.middle` allocations | 20 | **15** |
| `type.middle` bytes | 7.8 KB | **4.7 KB** |
| `layout.block.inline_attrs` self | 49.942 ms | **39.050 ms** |
| `open.cold_layout` | 10,326 us | 10,020 us |
| `layout.block.flow` (a control) | 46.938 ms | 51.442 ms |

A quarter of everything the layout allocates, and the scope that owned them 22%
faster. The end-to-end medians are a wash: the controls moved +2% to +10% the
wrong way in the same run, which is what a loaded machine looks like, and the
allocation columns are the ones that do not care. `peak_rss` is unchanged at
28.7 MB, because buffers that are allocated and freed inside a call were never
what the process was holding at its worst moment.

### Resolved: `wrapText` shortened by bytes, one shaping pass at a time

`InlineText.cpp`'s `wrapText` broke an overlong word by popping one *byte* off
the end and re-measuring the candidate on each pop. Two bugs in one loop: O(n)
shaping passes to shorten by a word, and a truncation that can cut a UTF-8
sequence in half and hand the renderer bytes that are not text.

`ui::breakToFit` is the fixed version -- the same bisection over code point
boundaries as `ellipsizeToFit`, which had the same two bugs, sharing its
`codePointStops` helper. `break_to_fit_never_cuts_inside_a_code_point` walks
every width from -4 to 60 pixels across a string of five two-byte characters and
asserts the answer is always an even number of bytes and always at least one
character; `break_to_fit_bisects_rather_than_walking` holds a 4,000-character
word to 16 measurements. Neither test needs a font.

It is on the empty-state and placeholder paths only, so this is a correctness
fix rather than a measurable one.

### Where a frame goes now

Three passes have landed on this, and they moved different things, so the numbers
are split rather than chained -- the first was measured on wall clock and the
other two on CPU time, and multiplying those together would be arithmetic rather
than measurement.

**The first pass** (fold stamping, the unchanged-update fast path, the block-key
carry-over, the allocation work), 460 KB note, 400-note library, 1600x1000,
Release, idle machine, wall clock:

| | at the commit before it | after |
|---|---:|---:|
| frame work p50 | ~8.4 ms (incl. the vsync wait; it could not separate them) | **0.19 ms** |
| first paint of the note | 126 ms | **33 ms** |
| `layout.update`, 60 frames | 189 ms | **58 ms** |
| `layout.fold_queries`, 60 frames | 168,060 | **2,801** |
| `layout.keystroke_relayout_median` | 1.075 ms | 0.292 ms |

**The second pass** (the harness clock, the row prefix sum, the counter batching,
the cache sweep, `setMetrics`), interleaved A/B, CPU time, min of four, 200 KB
fixture:

| | before | after |
|---|---:|---:|
| `layout.keystroke_relayout_median` | 0.294 ms | **0.232 ms** |
| `type.middle` | 292 us | **230 us** |
| `caret.block_to_block` | 138 us | **78 us** |
| `scroll.viewport_queries` | 8 us | **1 us** |
| `layout.update.scan_blocks` (the control) | 24.82 ms | 24.34 ms |

**The third pass** (the placement patch, the spliced rescan, the patched source
copy, the inline scan's buffers, the cache ceiling, and the `breakToFit`
correctness fix) is the one that took the remaining O(document) work out of an
edit. Interleaved A/B, CPU time, eight rounds, median = min and worst = max
across them, 200 KB fixture:

| | before | after |
|---|---:|---:|
| `type.middle` | 405 us | **28 us** |
| `type.near_top` | 408 us | **46 us** |
| `type.at_end` | 389 us | **11 us** |
| `backspace.middle` | 405 us | **28 us** |
| `newline.split_and_join` | 787 us | **108 us** |
| `caret.block_to_block` | 137 us | **2 us** |
| `scroll.idle_frame` | 12 us | **1 us** |
| `fold.toggle_heading` | 321 us | **196 us** |
| `type.middle` worst of eight rounds | 1,380 us | **98 us** |
| `backspace.middle` worst | 1,356 us | **144 us** |
| `newline.split_and_join` worst | 2,358 us | **269 us** |
| `resize.width_step` worst | 52,850 us | **24,485 us** |
| `layout.keystroke_relayout_median` | 0.406 ms | **0.026 ms** |
| `layout.keystroke_relayout_worst` | 1.337 ms | **0.147 ms** |
| `layout.keystroke_relayout_folded_median` | 0.561 ms | **0.196 ms** |
| `scroll.frame_relayout_median` | 0.018 ms | **0.006 ms** |
| **`peak_rss`** | 57.2 MB | **28.8 MB** |
| `type.middle` allocations / bytes | 21 / 8.9 KB | **15 / 4.7 KB** |
| `open.cold_layout` allocations / bytes | 41,696 / 14,039 KB | **32,092 / 10,889 KB** |
| `layout.blocks_walked` | 2,595,028 | **202,908** |
| `layout.source_bytes_copied` | 37,294,442 | **2,459,143** |
| `layout.update` self | 7.605 ms | **3.367 ms** |
| `layout.update.scan_blocks` self | 39.805 ms | **7.322 ms** |
| `layout.update.place_blocks` self | 76.597 ms | **40.095 ms** |
| `layout.update.resolve_folds` self | 10.108 ms | **5.380 ms** |
| `layout.update.map_blocks` self | 9.669 ms | **gone** |
| `layout.update.evict_cache` self | 11.295 ms | 24.257 ms |
| `layout.blocks_relaid` (the invariant) | 112,363 | 112,363 |
| `layout.visual_rows` (the invariant) | 2,882,912 | 2,882,912 |
| `layout.block` self (a control) | 25.290 ms | 25.618 ms |
| `layout.block.flow` (a control) | 52.198 ms | 51.007 ms |
| `layout.block.content_tokens` (a control) | 27.340 ms | 28.440 ms |
| `open.cold_layout` | 9,704 us | 9,743 us |

The bottom of that table is the point of it as much as the top.
`blocks_relaid` and `visual_rows` are *identical*, so both sides laid out exactly
the same blocks into exactly the same number of rows -- the change is entirely in
how much was done to find that out, not in what was produced. The three control
scopes inside `layoutBlock`, which nothing here touched, sit within 4%. And
`evict_cache` moved the *wrong* way on purpose: that is the cache ceiling trading
total sweep time for the size of the worst sweep, which is the `resize.width_step`
worst row four lines up.

It did **not** move first paint, and that is worth stating rather than leaving to
be inferred. `open.cold_layout` is 9.70 ms before and 9.74 ms after, and a real
session on a 336 KB note reports the same 40-58 ms of first-paint layout on both
sides, because that cost is glyph shaping in `layoutBlock`. What it moved there is
allocation -- 23% fewer, 22% fewer bytes -- and `layout.update` *self* time in the
same real session: 0.164-0.178 ms per frame before, 0.134-0.144 ms after. Nor did
it move a resize's *median*, which invalidates every key at once and so takes the
rebuild path by design; only the worst frame of one.

Rendering is unchanged across all three passes: the same 336 KB note captured
through `--screenshot` with a 401-note library is pixel-for-pixel identical.

### How the third pass is kept honest

Every change in it is an optimisation, and the only thing that makes an
optimisation safe is that it cannot be observed. Three tests carry that:

- **`layout_incremental_updates_match_a_layout_built_from_scratch`** walks a
  document through the edits a person makes and asserts after every one that the
  incrementally updated layout is indistinguishable from one built from scratch
  under the same inputs. It compares every block's position, height and every
  run on every line -- *and* the queries the surface actually asks, because the
  placement is patched now and a patch can leave a correct block sitting at a
  stale row. `offsetAt` over a grid, `rowRelative` up and down, `blockAt`,
  `blockRange`, `caretRect` and `selectionRects` are all in the comparison,
  because the row index is a separate array and nothing in `layout(i)` reads it.
- **`layout_incremental_updates_match_under_a_random_edit_sequence`** makes the
  same claim by machine. The scripted test covers the edits somebody thought of;
  the bugs a patch has are the ones that need a particular *pair* of consecutive
  updates -- an edit that shifts the block count, then a caret move above the
  shift, then a fold whose head is inside the block that moved -- and there are
  more such pairs than anyone will write out. Three seeds of 250 steps run in
  the suite; 30 seeds of 1,000 steps were run against the final code. The
  `index == 0` bug in the placement section was found at step 307 of one of
  them, and the missing restart margin in the splice at step 39 of another.
- **`layout_an_edit_rescans_and_replaces_only_what_it_touched`** asserts the
  *counters*, because a full rescan and a full replacement produce exactly the
  right answer and no correctness test can see them. It types one character into
  a 2,400-block document and requires fewer than 12 blocks rescanned, fewer than
  12 placed, under 64 source bytes copied, and one patch with no rebuild. Force
  the splice off and it reports "the edit rescanned nothing at all"; force the
  patch off and it reports "placed 2400 blocks of 2400".

Plus `blockscan_resuming_at_every_boundary_matches_a_full_scan` for the property
the splice rests on, and the sanitizer lanes, which is where a spliced array or
a patched buffer with an off-by-one shows up rather than as a wrong pixel.

### The fourth pass: startup, and the work nobody was counting

The third pass took the O(document) work out of an *edit*. The fourth went
looking outside the edit, and found that the largest single number in the
application had nothing to do with the layout at all.

**Indexing a library was quadratic in the library.** `notes_fts` declared its
`id` column UNINDEXED, which is fts5 for "store this and build no index over
it", so `DELETE FROM notes_fts WHERE id=?` was a full scan of the entire
full-text index -- once per changed file, against a table growing with every
file already done. The fts row is filed under the note row's own rowid now,
which is the one address an fts5 table can be found by in less than a scan.
The three scopes that found it -- `refresh.read_file`, `refresh.write_rows`,
`refresh.record_links` -- stay, because "the refresh is slow" was true for a
long time and said nothing about which of the three it was.

**Four blocks in five were being scanned for markup they do not have.**
`layout.plain_blocks` had been saying so for two passes: 89,759 of 112,300
relaid blocks reported the inline scan finding nothing. What those blocks paid
was not the four passes, which have little to walk in a short block, but the
fixed cost around them -- a zero-filled byte mask the size of the content, four
loop set-ups, a sort, and two vectors grown and dropped by any block carrying
markup at all. Every inline construct begins with one of seven bytes, so one
table-driven pass now settles it.

**A note with nothing folded was resolving folds on every edit.** The app's
predicate opened with `if(!anyFolded(noteId)) return false`, which is too late:
the layout still walked the block list, called through a `std::function` per
foldable block, and compared the all-zero answer against the all-zero one it
already held. A caller that offers *no predicate* says something stronger, and
the layout can act on it.

**A vector per visual row, and an estimate that was wrong by a factor of two.**
`VisualLine` owned its runs, so a document's layout allocated one vector per
row -- and freed them all again on the next cache sweep. The block vector's
`size / 48` estimate grew twice on the way to its real size, the last step being
the largest single allocation the application made; a block spans at least one
line, so the newline count is an exact bound rather than a guess.

Interleaved A/B, CPU time, min of eight rounds, 200 KB fixture and a 1,000-note
library:

| | before | after |
|---|---:|---:|
| `library_index.refresh_changed_files` total | 154.4 ms | **28.4 ms** |
| ...of which `refresh.write_rows` | 101.8 ms | **9.0 ms** |
| `library_index.search` self | 1.48 ms | **0.98 ms** |
| `fixture.app_state.open_select_and_list` self | 6.35 ms | 5.63 ms |
| `open.cold_layout` | 5,353 us | 5,035 us |
| `open.cold_layout` allocations / bytes | 32,092 / 10,889 KB | **28,879 / 9,123 KB** |
| `open.cold_layout` largest allocation | 1,470 KB | **826 KB** |
| `resize.width_step` | 6,316 us | 6,137 us |
| `resize.width_step` allocations | 28,047 | 25,241 |
| `type.middle` allocations | 15 | **11** |
| `scroll.idle_frame` allocations | 1 | **0** |
| `fold.toggle_heading` | 118 us | 106 us |
| `layout.block.flow` self | 29.78 ms | 22.88 ms |
| `layout.block.inline_attrs` self | 23.57 ms | 20.92 ms |
| `layout.update.scan_blocks` self | 4.19 ms | 3.50 ms |
| `layout.update.resolve_folds` self / calls | 3.29 ms / 513 | 2.88 ms / **31** |
| `layout.block` self | 15.01 ms | 21.14 ms |
| `peak_rss` | 28.7 MB | 27.7 MB |

`layout.block`'s own self time is the one row that went the wrong way, and it
went there on purpose: the run array's reserve moved out of the flow and into
`layoutBlock`, so the two together are flat and the scenario totals above are
what actually moved.

**Every other counter is byte-identical across the two sides** -- `blocks_relaid`,
`visual_rows`, `inline_spans`, `fold_queries`, `blocks_walked`, `source_bytes_*`,
all of them. The only new rows are the three counters this pass added and three
extra SQLite statements from the schema migration. Nothing here changed what the
application computes; it changed how much was done to compute it.

And in a real session -- 185 KB note, 401-note library, 1600x1000, the real
renderer and the real faces, four interleaved rounds, min:

| | before | after |
|---|---:|---:|
| `app_state.open_or_create_library` total | 40.8 ms | **16.4 ms** |
| `library_index.refresh_changed_files` total | 36.6 ms | **11.8 ms** |
| `layout.fold_queries` | 880 | **0** |
| first paint (`layout.update.place_blocks` total) | 10.46 ms | 10.33 ms |

**First paint did not move, and that is the honest headline.** Opening a note is
still what it was, because what it is is glyph measurement inside `layoutBlock`,
and nothing in this pass touched that. What moved is the *other* half of a cold
start: the second and a half of a launch that was the index, not the note.

The same note captured through `--screenshot` in `live`, `viewer` and `split`
panes with the sidebar and the right panel up is pixel-for-pixel identical
before and after, and the asan, ubsan, tsan and clang `-Werror` lanes are clean.

### Resolved: a wikilink kept its colour after the library moved under it

Not a performance finding, but found by one. Whether a `[[target]]` resolves
decides a run's colour, and it was the one input to a block's layout that is not
a function of the block's own bytes -- so it was not in the cache key, and a link
that started or stopped resolving kept the colour it had until somebody happened
to edit that particular block. Create the note a pending link points at and the
link stayed pending; delete it and the link stayed confident.

`LayoutOptions::wikiLinkRevision` sits beside the two stamps that were already
there, and is mixed into the geometry -- which is to say into every cache key,
because a change to the library can change the answer for any link anywhere in
the note. It moves only when the app drops its candidate list: a note created,
renamed, deleted, or the library re-listed, each of which already re-reads the
library, so the relayout it forces is the cheapest thing happening at that
moment.

The lesson is the general one about stamps. `sourceRevision` and `foldRevision`
are optimisations -- withhold them and the layout is slower and still right.
This one is not: withhold it and the layout is wrong. An input a cache key
cannot see has to be stamped, and the way to find the others is to ask, of every
`std::function` in `LayoutOptions`, what happens when its answer changes on its
own.

### What an edit costs now, and what is left in it

A keystroke on a 200 KB note is 16 us and 11 allocations against a 2 ms budget,
and it is worth naming what is still in it, because none of it is a block walk
any more:

- **`matchEdges`**, two `memcmp` passes that together cover the note -- prefix
  until the first difference, suffix until the first difference from the end. On
  a small edit that sums to about the whole buffer, so ~200 KB of `memcmp`. It
  is what locates the edit, and the caller does not say where it typed. This is
  now most of the keystroke: `layout.edit_bytes_matched` is 34,835,299 over the
  run against `layout.source_bytes_copied`'s 2,459,143.
- **The source memmove**, `layout.source_bytes_moved`, averaging 29 KB an edit
  over the run. A flat buffer has no way around dragging its tail.
- **The block tail's `start`**, one integer add per block after the splice
  point. On an edit near the top that is every block in the note. It was four
  adds until `SourceBlock` started holding its payload relative to its own
  start.
- **`resolveFolds`**, but only for a note that has a fold in it, and then only
  from the fold spanning the edit downwards. One with none skips the resolution
  entirely -- `layout.fold_resolutions_skipped` is 519 of the harness's
  updates -- and one with a fold resolves `layout.fold_blocks_resolved` blocks
  rather than `layout.blocks`.
- **The placement's delta pass**, `layout.blocks_shifted`, when the edit changed
  its block's height or line count: one float add and one integer add per block
  below it. Zero when it did not, which is most keystrokes.

Every one of those is a linear pass over integers or bytes with no branches, no
hashing and no pointer chasing, which is why they add up to 16 us where the walks
they replaced were 337. Three of them are named as open items below.

### Open: an edit still touches every block below it

`layout.blocks_shifted` reads 451,230 over the run and the block tail's `start`
shift is the same shape: an edit near the top of a note updates a position on
every block under it, even though nothing about those blocks changed except where
they sit. (The block half is one integer add per block now rather than four --
see `SourceBlock` below -- so what is left of this is the placement's.)

Both are the same design decision -- positions are *materialised*, as an absolute
`top` per block and an absolute row index per block -- and the alternative is to
store per-block heights and answer `blockTop(i)` from a Fenwick tree or a segment
tree in O(log n). That makes an edit O(log n) and makes every position query
O(log n) as well, and the position queries are the ones on the render path, asked
per visible block per frame. At the current numbers the materialised version wins
on both counts, so this is written down as the thing to reach for if the shift
ever shows up rather than as a fix waiting to happen.

The measurement that would justify it is `layout.blocks_shifted` per update
against `layout.blocks`: today it is 1,812 against 9,612 on average, and a
keystroke that does not rewrap its own block moves nothing at all.

### Resolved: `resolveFolds` was O(blocks) on every edit *that has a fold in it*

Every edit used to re-resolve the fold state over the whole block list -- a
`foldableKind` test per block and a predicate call per foldable one -- and then
a `memcmp` of the result against the previous generation to tell the placement
which blocks a fold change moved. It ran on every edit whether or not anything
in the note was collapsed, which for most notes most of the time is a walk of
the document to produce the all-zero array it was already holding.

Half of that went first. A caller that offers **no predicate at all** is saying
something stronger than a predicate that always answers false, and the layout
acts on it: if nothing was hidden last time either, an all-zero resolution of
the right length is the answer already in hand. `Application` leaves
`PageFolds::collapsed` unset for a note with no folds, and
`layout.fold_resolutions_skipped` reads 519 of the harness's updates.

The other half is the note that *does* have a fold in it, and it is a
resumption rather than an incremental structure:

> `hidden[j]` depends only on blocks `[0, j]`. A fold reaching `j` has to have
> covered every block between its head and `j`, and the scan that decides how
> far a head reaches stops at the first block that breaks it -- so it never
> looks past `j` to answer for `j`.

So everything before an edit keeps the answer it had, back to the head of
whatever fold spans the seam. `resolveFoldsAfter` walks back from the carried
prefix over the hidden run to that head -- one byte scan of the run, not of the
note -- copies the head of the previous resolution forward, and re-resolves from
there. An edit at the bottom of a folded note walks the blocks after the fold it
sits in; an edit at the top still walks the note, which is the case that cannot
be resumed.

| harness run | before | after |
|---|---:|---:|
| `layout.fold_queries` | 95,948 | 76,640 |
| `layout.update.resolve_folds` | 3.27 ms | 2.87 ms |
| `layout.keystroke_relayout_folded_median` | 115 us | 71 us |

`layout.fold_blocks_resolved` is the counter that says how much of a note a
resolution actually walked; read it against `layout.blocks`. The harness's
folded scenario collapses the *first* heading and types in the middle, which is
about the least this can save -- a real note with a collapsed section above the
cursor saves the whole of it.

Two details worth keeping. The array is `resize` + `memcpy` of the head +
`memset` of the tail rather than `assign` + `memcpy`, because `assign` memsets
the whole thing and then has the head written over it -- three linear passes
where one will do, and doing it the wrong way round measured *slower* than the
per-block walk it replaced. And `anyHidden_` is carried forward rather than
recomputed, because asking whether anything in the carried head is hidden is
exactly the scan this avoids: carrying it can only leave the flag set when
nothing is hidden any more, and the one thing that reads it also requires the
caller to have withdrawn its predicate, at which point the resolution is a full
one and answers exactly.

### Resolved: `SourceBlock` was 88 bytes and held a `std::string`

This stopped being about *walks* over the block list several passes ago. What it
was still about was the two places an edit moves the array around:

- **A block-count change memmoves the tail of it.** `newline.split_and_join` was
  92 us against `type.middle`'s 23, and the difference was almost exactly the
  memmove: 9,612 blocks at 88 bytes is 846 KB, plus the four parallel placement
  arrays sliding with it, for one pressed Return.
- **Every edit shifted the tail's offsets**, and there were four `std::size_t`
  of them per block -- `start`, `end`, `contentStart`, `contentEnd`.

The block is 40 bytes now and everything but `start` is held relative to it:

```
std::size_t   start          // absolute, and the only field that is
std::uint32_t length
std::uint32_t contentBegin, contentLength     // from start
std::uint16_t infoBegin, infoLength           // from start
std::int32_t  ordinal
std::int16_t  listDepth
std::uint8_t  level;  BlockKind kind;  bool ordered, checked
```

`end()`, `contentStart()` and `contentEnd()` are one add each, and the offset
shift after a splice is **one** add per block instead of four, because moving a
block moves its payload with it by construction. `info` -- the fence language or
the callout kind, empty for almost every block -- is a span of the source rather
than an owned string, which is also what stops it dangling: the small-string
optimisation puts a short one's bytes *inside* the string object, so a
`string_view` into a source buffer that gets swapped out is a use-after-free
waiting for a long enough fence language. Sixteen bits are enough for its
offsets because both constructs live on the block's first line.

| 200 KB note | before | after |
|---|---:|---:|
| `newline.split_and_join` | 104 us | 44 us |
| `type.near_top` | 44 us | 21 us |
| `type.middle` | 26 us | 15 us |
| `layout.keystroke_relayout_median` | 17 us | 14 us |
| `layout.update.scan_blocks` (over the run) | 5.80 ms | 4.92 ms |
| `open.cold_layout` allocated | 9,123 KB | 8,673 KB |
| largest single allocation | 826 KB | 375 KB |

No counter moved: the two sides did exactly the same work, which is what says
the 38% off a pressed Return is bytes not moved rather than work not done.

**Two things cost more, and both were measured rather than assumed.** Packing
means the scanner has to subtract, so `scanOneBlock` writes `length` and the
content span as differences from `start`. Doing that at every branch of the
scan -- the scanner decides a block's payload in up to three places -- was 20%
on the un-lent block transforms; tracking the offsets as absolutes and packing
once, on the way out, brings it back to 2-8%, which is the un-lent path paying
a subtraction per block for the memmove every other path saves. The lent
transforms, which are what the app actually runs, did not move at all.

The other was **bitfields**, and they are worth writing down because they looked
free. Packing `level`, `ordered` and `checked` into four bits and two flags also
reaches 40 bytes -- and cost 25% on the scan (137 us to 170 us on a
minimum-of-sixty bench), because every write to one is a read-modify-write of
the byte the others live in. Sixteen-bit info offsets reach the same 40 bytes
with plain fields and plain stores.

`peak_rss` is 29.8 MB against 30.0.

### Measured and declined: the staging tokens are built only to be thrown away

`layoutBlock` tokenizes a block into a `Token` vector and then flows that vector
into runs. The tokens are read once, in order, by exactly one consumer, and
`Flow` needs no lookahead beyond the whitespace and the unbreakable cluster it
already holds back -- so the staging vector could go entirely, and the tokens
could be pushed into `Flow` as they are made.

**What it would save, measured.** A `layout.block.stage` timer wrapped around
the whole of the staging -- `layout.block.content_tokens` covers only the
marked-up path, which is one block in five, so it was never the number to weigh
this against:

| over one harness run | self ms | of `layout.block`'s 615 ms |
|---|---:|---:|
| `layout.block.stage` (inclusive) | 112.1 | 18% |
| ...of which `layout.block.inline_attrs` | 50.3 | the inline scan; survives |
| ...of which `layout.block.content_tokens` | 33.7 | the tokenizer; survives |
| ...of which staging itself | 28.1 | 4.6% |
| `layout.block.flow` | 475.9 | 77% |

Streaming removes the 28.1 ms of group bookkeeping and a share of the flow's
walk over those groups. It does not remove the inline scan, and it does not
remove the tokenizer -- a streaming tokenizer still builds each `Token`, it just
hands it over instead of filing it. **The ceiling is around 5%**, and it is paid
for with three things:

* `out.runs.reserve(tokens)` stops being exact. The flow emits one run per
  staged token, so the reserve is currently a count rather than an estimate;
  without the staging there is nothing to count, and the runs vector goes back
  to doubling its way there. That is an allocation regression traded for a CPU
  saving.
* the fenced-code path, whose opening marker is either a line of its own or
  rides in front of the first line of code, has to be turned inside out.
* `Flow`'s pending-space and cluster buffers stop being *indices* into a group
  and have to hold the tokens themselves, which is where the `Token`s' strings
  then live.

`layout.tokens_staged` is 2,446,091 over a run and is the counter to re-read if
the shape of the tokenizer changes. Written down with its number rather than
left as an open item: at 5% against an exact reserve it is not worth the
restructuring today, and the next reader should not have to measure it again to
find that out.

### Resolved: the harness could not see the font path

Every budget in `tools/PerfMain.cpp` measured against `stubMetrics()`, a
fixed-advance stand-in, because the core library has no fonts in it. That is the
right call for a core-level benchmark and it is also why the largest cost in the
app -- glyph shaping to open a note -- was invisible to `run-checks.sh perf` for
four passes and only showed up in a real session.

The obstacle was never the measurement. It was that `micronotes_perf` linked
`micronotes_core`, and everything that knows about a font -- `ui::Fonts`,
`ui::Draw`, `PageView` -- was compiled into the executable instead of into a
library. Splitting `micronotes_shell` out (the same change that made `src/app/`
testable) removed it: the harness links the shell, constructs a real
`ui::TextRenderer` against a null SDL renderer -- measurement needs SDL_ttf, not
a window -- and lays the note out through `app::documentMetrics`, which is the
same measure and line height `PageView` uses, at the same type scale.

**The fixture had to change with it, and that is the part worth reading.** The
200 KB note the other lanes share is written by a loop, so it repeats a few
thousand distinct words several thousand times. A cold open of it through a real
face is **99.2% measure-cache hits** and 6.4 ms -- barely above the stub's 5.9 --
so pointing the existing fixture at a real font would have produced a lane that
looks like it measures shaping and does not. `uniqueWordMarkdown` writes prose
whose every word is different, and each pass gets its own note, because the
measure cache belongs to the renderer and outlives the layout: run one note four
times and only the first pass shapes anything.

| 200 KB, one cold open | median | `render.text_measure_calls` | hits |
|---|---:|---:|---:|
| stub metrics | 5.9 ms | -- | -- |
| real face, repeated words | 6.4 ms | 72,090 | 99.2% |
| real face, every word different | 65 ms | 62,510 | 53.2% |

Ten times the cost for the same number of measurements. The 53% that hit are the
*spaces* -- one token per gap, always `" "`, always cached -- so the miss column
is one shaping pass per word and nothing else.

That is the number the harness could not see: **opening a 200 KB note of
ordinary prose is 65 ms of glyph shaping inside the layout alone**, on an idle
machine. It also moves more with the machine than anything else here -- the same
binary measured 200 ms while a parallel build was running, on CPU time, which is
why `kFontShapingBudgetMicros` is six times the observed figure and the counters
beside it are what a change is actually judged on.

What is still not in the lane is a *draw*: no window, no textures, no present.
It says what shaping and measuring cost and nothing about rasterizing. For that,
`tools/session-compare.sh` remains the instrument.

`src/ui/TextMeasureCache.{h,cpp}` is the answer on the app side, and a real
session says so: over a 185 KB note it reports 63,449 measure calls against
62,473 hits -- **98.5%**, 976 misses -- because a real note *is* repetitive.
The shaping is not the cost once the note is open. What first paint costs is the
run building around it: `layout.block.flow` is 7.3 ms of a 10.3 ms first paint,
and the measure calls inside it are a hash and a probe rather than a face.

Two things were tried against that 27 ns per measure and did **not** move it, so
that the next reader does not spend the afternoon again. Removing the two
`perf::addCounter` calls from `TextRenderer::width` -- on the theory that two
relaxed atomic read-modify-writes on a process-wide cacheline were a third of
the call -- measured no difference across three interleaved sessions. And the
direct-mapped measure cache is not thrashing: 98.5% is not a hit rate with room
in it. What is left is the token staging below, which is the next thing to try.

### Resolved: `matchEdges` compared the whole buffer to find a one-byte edit

Every edit ran two `memcmp` passes -- forward to the first differing byte,
backward to the first differing byte from the end -- and on a small edit those
two sum to about the length of the note. ~200 KB of `memcmp` to locate one typed
character, and it was the largest single item left in a 16 us keystroke.

`layout.edit_bytes_matched` read 34,835,299 over the harness run against
`layout.source_bytes_copied`'s 2,459,143: **fourteen bytes read for every byte
that moved.**

It was there because `update` was handed a buffer and no account of what
happened to it. The caller does know, and now says so. `MarkdownEditor` records
the span of every mutation it makes -- `markChanged` takes the span rather than
deriving it, so a new mutation cannot be added without stating what it touched
-- and `LayoutOptions::editedSpan` carries that span with the two source
revisions it took the buffer between.

The honest form of the change, and the one that was made: **keep the comparison
and use the claim to bound it.** The window starts at the claim instead of at
the ends of the buffer, and the two loops then run exactly as they did -- they
only ever widen the matched prefix and suffix, which narrows the window, so a
true claim gives byte-for-byte the answer the full comparison gives. What
changes is how much has to be read to get there.

Three things make a bad claim cost speed rather than correctness:

- **The stamps are checked, not trusted.** `claimFor` accepts a claim only when
  its `fromRevision` is the stamp of the buffer the layout is standing on and
  its `toRevision` is the stamp of the buffer it was handed. A frame that
  handles two keystrokes produces a claim for the second edit only, its `from`
  does not match, and it is discarded.
- **The arithmetic is checked.** The bytes a claim leaves outside itself have to
  come to the same count on each side -- `old.size() - oldEnd == new.size() -
  newEnd` -- and nothing but a real edit of that span does.
- **An absent claim is the old path.** Every stamp defaults to zero, which means
  "cannot say", so the tests and the perf harness still compare bytes. That is
  why `layout.edit_spans_compared` reads 183 and `edit_spans_used` reads zero in
  `run-checks.sh perf`: the fixture has no editor behind it.

`layout.edit_bytes_compared` is the new counter and the one to watch. It counts
what the two passes actually **read**, where `edit_bytes_matched` counts the
window they concluded; without a claim the two are equal, and the gap between
them is the note the caller saved being read. On a 193 KB note, a one-byte
insertion in the middle:

| | bytes read to find the edit |
| --- | --- |
| comparing | 193,780 |
| with the caller's span | **under 1,024** |

`LayoutTests` pins both halves: `layout_uses_the_callers_edited_span_instead_of_comparing_the_note`
asserts the byte count *and* that the result still agrees with a layout built
from scratch, and
`layout_discards_an_edited_span_that_describes_another_pair_of_buffers` asserts
that a stale stamp, impossible arithmetic and an absent claim all fall back and
all still land on the right answer. The random-edit walk carries a claim through
every insertion and deletion of its 250 steps, against a from-scratch layout at
each one.

One thing that looks like a free win here and is not: skipping `sourceMatches`
when the caller's source stamp says the buffer moved. It reads like a redundant
second pass over the same half of the note, and it is not one -- `sourceMatches`
compares the *lengths* first, and every insertion and deletion changes the
length, so on an ordinary keystroke it already returns without reading a byte.
Four interleaved rounds put `type.middle` at 16 us with and without it. It was
written, measured, and reverted.

### Resolved: the live page's hooks were rebuilt every frame

`drawLive` constructed a `PageViewHooks` and a `PageFolds` and moved them into
`ui.livePage` on every frame. Between them that is five `std::function`
assignments, and each closure captures more than a `std::function`'s inline
buffer holds -- so it was five heap allocations and five frees per frame, for
closures whose captures (the renderer, the text renderer, the runtime) do not
change for the life of the process.

All five are installed once now, and `PageView::wired()` is how the caller knows
whether it has done it. The three hooks were the easy half. The two fold
closures looked like the hard half and were not: they capture the *runtime*
rather than the note, and read the current selection and the current buffer when
they are called, which is what makes installing them once correct rather than
merely cheaper.

Two things did have to move out of the closures, because they are per-frame data
rather than per-frame behaviour:

- `wikiLinkRevision` was a field of the hooks struct. It is
  `PageView::setWikiLinkRevision` now.
- Whether the selected note has *anything* collapsed was expressed by leaving
  `PageFolds::collapsed` unset, and that distinction matters: a caller offering
  no predicate lets the layout skip resolving folds over the whole block list,
  where a predicate that always answers false costs a call per foldable block
  per edit (see `resolveFolds`, above). The predicate stays installed and
  `setFoldsActive` decides whether it is passed on, so the skip survives -- 
  `layout.fold_resolutions_skipped` reads 60 of 60 frames before and after.

`drawLive` itself moved to `src/app/LivePage.cpp` with the wiring, which is 70
lines out of `Application.cpp`.

### Tried and rejected: pooling what the cache sweep frees

`layout.update.evict_cache` is around 20 ms over nine sweeps, and a sweep is
almost entirely `free`: four vectors per evicted block, plus any run string long
enough to escape the small-string optimisation. A resize then immediately
allocates the same shapes back, because every key changed with the geometry.

The obvious answer is a pool: the sweep moves each evicted `BlockLayout` onto a
free list instead of destroying it, and the relayout takes one and clears its
vectors -- which keeps their capacity -- instead of building from empty. It
turns the sweep's frees and the relayout's allocations into a move each.

**It was built, measured against no pool over four interleaved rounds, and
thrown away.** Two bounds were tried: `kSpareEntries` (256, the same spare the
cache itself allows) and one whole generation of the document.

| `resize.width_step`, minimum of 4 | no pool | 256 | one generation |
|---|---:|---:|---:|
| median | 8,613 us | 8,581 us | **11,108 us** |
| allocations | 25,241 | 24,877 | 17,020 |
| bytes | 6,750 KB | 6,635 KB | 3,590 KB |
| `layout.update.evict_cache` | 19.6 ms | 20.4 ms | 16.4 ms |
| `peak_rss` | 31.0 MB | 31.0 MB | **48.8 MB** |

The generation-sized pool does everything it was supposed to -- a third fewer
allocations, half the bytes, three milliseconds off the sweep -- and is **31%
slower**, in every round, while costing 18 MB of peak RSS. Recycling a
`BlockLayout` is two moves of four vectors plus a `clear()` that runs a
destructor for every `TextRun` in it, where dropping the whole layout frees the
run array in one go; the recycled capacities are then the wrong size for the
next block and grow anyway; and holding a document's worth of layouts alive
across the relayout is a working set the sweep was removing on purpose. The
bounded version is a wash on every column, which is the same answer with less
of it: it covered 2,048 of 48,228 evictions.

So the sweep stays a sweep. The entry that used to sit here said "measure
`peak_rss` first and `resize.width_step` second, in that order" -- that was the
right instruction and this is the answer: the memory is real and the speed is
negative.

### Resolved: the cold scan's block vector grew twice, and the second growth was the app's largest allocation

`scanBlocksInto` reserved `source.size() / 48 + 8` blocks, and prose runs nearer
one block per 20 bytes, so a cold vector grew twice: 4,166 -> 8,332 -> 16,664
entries at 88 bytes each. `open.cold_layout` reported a 1,470 KB largest single
allocation, which was exactly that last step.

A block spans at least one line, so the newline count is an *exact upper bound*
rather than an estimate, and counting newlines is one vectorised pass over bytes
the scan is about to read anyway. It is asked for only when the standing
capacity is already too small, which on the rescan that runs per keystroke it
never is. `open.cold_layout`'s largest allocation is 826 KB and its byte total
10,889 KB -> 9,123 KB.

The general shape is worth keeping: an estimate that is deliberately low to
avoid over-allocating is a growth curve in disguise, and the
largest-allocation column is what makes that visible. Prefer a bound you can
derive to a constant you guessed.

### Resolved: `pushNoteShortcuts` was O(favorites x notes)

`SidebarModel.cpp` resolved each favourite and each recent by a linear scan of
the whole note list -- thirteen shortcuts against a thousand notes is thirteen
thousand string compares to draw thirteen rows. `OrganizationService` keeps an
id index now, built by the same call that finalises the note list, so its keys
(views into the ids in that list) cannot outlive or predate it. `findNote` is a
hash lookup, `noteById` is the same lookup without the copy, and the sidebar
takes the borrow.

That index also fixed the shape one level up: `currentNotes()` resolved every
search result through the linear scan, so a query returning 200 rows over a
1,000-note library was 200,000 string compares to fill a list the caller already
held a reference to.

### Resolved: the library directory was walked three times on startup

All three are gone, and the deeper duplication behind two of them with them.

`OrganizationService::folders()` ran its own `recursive_directory_iterator` a
few microseconds after `notes()` had walked the same tree, because the only
thing it needs that a list of notes cannot give it is the directories with *no*
notes in them. `Library::walk` reports those alongside the files, and both lists
are memoised off the one scan. That walk had no counter, which is why it
survived: `library.directory_entries_visited` did not move when it went away.

Every caller also re-derived each note's folder with `lexically_relative` plus
`parent_path` -- two path allocations, done per note per caller by the folder
filter, the tree, the sidebar, the breadcrumb, the wikilink placer and the note
mover, over a list that had not changed. `NoteListItem` carries `folder` now.
And `folders()` counted notes into folders with a `find_if` over the folder list
per note, which on a library filed into as many folders as it has notes is
quadratic; it is a hash lookup per note.

The third walk was the organization service's own, and behind it was the real
duplication: the index refresh had *just* read every file and held each one's
id, path and title in SQLite, and the organization service then opened all of
them again for those same three fields plus tags and icon. Two columns closed
it. `notes` carries `tags` and `icon` (schema 3, and the index is a cache of
what is on disk, so an older shape is dropped and refilled by the next refresh
rather than migrated), `LibraryIndex::notes()` is one `SELECT`, and the
directories come from the refresh's own walk through
`LibraryIndex::directories()`.

| | before | after |
| --- | --- | --- |
| `app_state.open_select_and_list` (self) | 5.689 ms | **0.704 ms** |
| `library.note_files_calls` (4 refreshes, 1,001 notes) | 5 | 4 |
| `library.directory_entries_visited` | 5,018 | 4,014 |
| note list built by | a walk + 1,001 file opens | `library_index.notes`, 0.616 ms |

`library.note_rows_selected` is the counter that says the list came out of the
index. The walk is still there as the fallback for an index that will not open
-- a library whose SQLite file cannot be written must still list its notes --
and `organization_falls_back_to_the_tree_without_an_index` is the test that says
so.

### Resolved: the reading pane measured the whole note twice a frame

The reading pane renders the note through md4c, and it did so with no cache of
any kind: one walk of the document to find how far it scrolls, and a second walk
to paint it, both measuring every block's inline runs, on **every frame** --
including the frames a hover caused.

Measured on a 242 KB note at 1600x1000, Release, headless, 60 frames, twice
each way:

| | `shell.content` per frame | `frame.draw_micros` (60 frames) |
| --- | --- | --- |
| before | 7.6 ms / 7.5 ms | 466,648 / 456,918 |
| after | **0.37 ms / 0.36 ms** | **30,654 / 29,396** |

One walk now, memoised on the parsed note and the geometry, and the draw reads
block tops out of it and bands to the viewport with the same two binary searches
the live surface and the sidebar use (`ui::rowBand`). `viewer.blocks_measured`
reads 5,611 once -- the note's block count -- and `viewer.blocks_drawn` reads 23
a frame. `viewer.layout_builds` is 1 against `layout_reused`'s 59.

Two things came free with having one walk instead of two. The image cache grew a
`generation()`, because a texture that finishes loading changes the height of the
block that shows it and the memo has to key on that. And the two walks had
**disagreed** in three places -- an Html block's bottom spacing (the measure
passed the real answer, the draw passed `false`), an image's rounding, and the
callout label's case -- each of which is a scroll extent or a box that does not
match the text in it. Two parallel walks of the same blocks is a shape that
cannot be checked; one walk cannot drift from itself.

The pane became `src/app/ReadingPane.cpp`, 247 lines out of `Application.cpp`.
What was *not* resolved was that it was still a second renderer for the same
Markdown; the seventh pass below is the merge into `PageView`, and the file is
gone.

## The fifth pass: what the three instruments could not see

The four passes above are all about the document layout, and by the end of them
a keystroke on a 200 KB note was 16 us against a 2 ms budget. The fifth pass
started from a different question -- *what is on the keystroke path that none of
the three instruments is pointed at?* -- and the answer was most of the
keystroke.

The gap was structural, not an oversight. Counters and scope timers only report
what someone thought to instrument, and everything in `tools/PerfMain.cpp`
enters through `doc::` and `library::`. Nothing in the harness goes through
`src/app/`'s key handling, its shell surfaces, or `AppState`'s writes to disk.
So the instruments were sharp and they were all aimed at the same wall.

Two things closed it, and both are now standing practice:

- **A real session is the instrument for anything above `doc::`.** `Xvfb` plus
  `--screenshot` prints both tables over the real renderer for a command's worth
  of effort (see "Running a real session without a screen"). Every finding in
  this pass except the first came out of reading that table and asking why a
  panel cost more than the note.
- **The pixels are the regression check.** Every change below was verified by
  capturing the same note in `live`, `viewer` and `split` from both builds and
  `cmp`-ing the files. A layout or paint optimisation that cannot be observed is
  safe; one that can is a bug, and this is the cheapest possible proof.

### Resolved: every keystroke waited for two `fsync` barriers

`markEdited` ran on every typed character, every Backspace and every Delete,
and called `writeFileDurably` straight through: temp file, write, `fsync`,
rename, `fsync` the directory. Two durability barriers, synchronously, on the
thread drawing the window.

| | median | worst |
|---|---:|---:|
| 200 KB note, quiet disk | 1,117 us | 4,000 us |
| 1 KB note, quiet disk | 961 us | 2,045 us |
| 200 KB note, disk busy | 8,707 us | 35,643 us |

A 1 KB note costs the same as a 200 KB one, which is what says the cost is the
barriers and not the bytes. Against a 2 ms frame budget it is a dropped frame
per character, and it was **twenty-five times the whole layout update** the four
previous passes were about.

The write has to be durable. Waiting for it never did. `library::RecoveryStore`
takes a post as a copy into a one-slot-per-note mailbox plus a notify, and one
writer thread drains it, latest-wins per note:

| | before | after |
|---|---:|---:|
| UI thread, per keystroke | 1,117 us | 7.8 us |
| durable writes for a 200-character burst | 200 | 2 |

So the disk does *less* work as well as later work: a burst of typing collapses
to one write of the newest text, which is the only version anyone would want
back. `recovery.posts` against `recovery.writes` is the counter pair; writes
climbing to meet posts means the coalescing has stopped.

`clear` goes through the same mailbox as a tombstone rather than being performed
on the spot, and that is correctness rather than tidiness: a direct removal
races the saves already queued behind it, and losing that race leaves the app
offering to recover a draft it had already committed.

What it gives up: the copy on disk trails the buffer by whatever write is in
flight, about a millisecond, instead of being current at every character. The
*real* save was already debounced 1.2 s, so this is the tighter of the two by
three orders of magnitude.

### Resolved: every structural key rescanned the whole note

Every operation in `doc/Edits.h` needs the buffer's block partition to find the
one block it acts on, and every one derived it with a fresh `scanBlocks` -- a
pass over every byte plus a `vector<SourceBlock>` at 88 bytes a block.

| transform, 200 KB / 12,614 blocks | scanning | lent |
|---|---:|---:|
| `continueList` | 373 us | 57 ns |
| `outdentOrUnwrap` | 375 us | 37 ns |
| `toggleTodo` | 384 us | 40 ns |

Backspace paid it once. Enter paid it twice -- `closeFence`, then
`continueList` -- and three times when it landed on an empty nested list item,
because `continueList` delegates to `outdent`. So Enter was ~750 us of block
scanning against the 40 us of layout it then triggered.

`DocumentLayout` was holding that exact partition the whole time, spliced rather
than rescanned on every keystroke -- which is what the third pass above was
*about*. Each operation now takes a trailing `BlockSpan`; `blocksAt(revision)`
is what makes lending safe, because it answers only for the revision the layout
was built from, and every buffer mutation in `MarkdownEditor` goes through
`markChanged`. A caller whose buffer has moved gets an empty span and scans.

`edits.*_lent_200kb` measures the borrowed path beside the scanning one, under a
40 us budget rather than the 4 ms the fallback keeps.

### Resolved: a selection cost the document rather than the window

`selectionRects` was bounded by the document. Ctrl+A on a 200 KB note selects
6,600 visual rows; a window shows forty. Every frame the selection was up built
all 6,600 into a fresh vector and drew the forty on screen.

Twice per frame, in fact: `drawToolbar` made the same unbounded call and used
exactly two of the rects, `front()` and `back()`, to decide whether the
formatting toolbar sits above the selection or below it.

| per frame, 200 KB / 11,571 blocks | before | after |
|---|---:|---:|
| selection rects | 190-670 us | 0.5 us |
| toolbar anchor | the same call again | 0.1 us |

So a frame with a select-all up spent 0.4-1.3 ms of its 2 ms budget building
rects to discard. This is the same shape as the find highlighter two functions
below it, which had been given a visible band for exactly this reason -- and the
selection is the worse of the two, because a find highlight needs a query typed
and a selection needs Ctrl+A. `selectionRectsInto` takes the band and appends
into a caller-owned vector; the band applies to rows as well as blocks, because
a fenced code block is one block that can be thousands of rows long.
`selectionEnds` answers the toolbar's question without the middle.

`select_all.rects` and `select_all.toolbar_anchor` hold it under a 60 us budget
and print how much of the selection the band admitted: 19 rows of 6,408.

### Resolved: the right panel cost more per frame than the note beside it

All three of its views were derived from scratch every frame, each expensive
differently: Outline ran `scanBlocks` over the whole note for its headings,
Backlinks ran a SQLite query, and Tags called `selectedNote()` -- which **reads
the note back off disk**, parses its front matter and copies its body -- to draw
a row of chips.

Real session, 400-note library with a 200 KB note open, `shell.right_panel` per
frame:

| view | before | after |
|---|---:|---:|
| Outline | 0.301 ms | 0.019 ms |
| Backlinks | 0.251 ms | 0.033 ms |
| Tags | 0.534 ms | 0.015 ms |

`page.draw` on the same frames is 0.16 ms. So an idle frame was spending two to
three times as long on the panel beside the note as on the note, and none of it
could change unless the note or the library had.

Memoised on keys, the way `PageHeader` already does it, and for the reason its
comment gives: a flag has to be raised at every mutation site and the one that
forgets leaves the panel describing a note that has moved on. Two keys, because
the views do not share inputs -- the outline is a function of the buffer and has
to move while the user types; backlinks and tags come from the library and must
not. `right_panel.outline_builds` / `_reused` and `right_panel.library_builds` /
`_reused` read 1 against 59 over 60 frames.

### Resolved: typing in the search box froze the window for a quarter second, per character

`rebuildSidebarRows` trimmed every matching line of every result to the
sidebar's column up front. One trim is ~0.25 ms: it measures the whole line,
bisects for the head cut, then bisects again for the tail -- and every probe is
a string nothing has measured before, so the measure cache cannot help and each
is a real shaping call. A 200-result query carries 600 of them.

Interleaved A/B over a real session, four rounds alternating the two builds,
worst frame in `shell.sidebar`:

| | before | after |
|---|---:|---:|
| round 1 | 306 ms | 53 ms |
| round 2 | 232 ms | 37 ms |
| round 3 | 308 ms | 24 ms |
| round 4 | 206 ms | 28 ms |

And that frame happens on every keystroke of the query, so a six-character
search was well over a second of dead window.

The row list is still every result, because the heights have to add up to a
scrollbar -- but a row's height needs only *how many* lines it will show, which
is a count and not a measurement. The trimming moved to the draw, per row it is
about to paint: `sidebar.snippets_trimmed` reads 36 where it read 600. Rows keep
their own trimmed lines, so scrolling back over one does not trim it again, and
the scroll-shift that keeps a scroll off the rebuild path is untouched.

What remained in that ~30 ms was mostly the 36 trims themselves, and most of
*that* is gone too. Both searches inside `snippetAroundMatch` used to bisect
down from the whole line, so the early probes measured hundreds of bytes to find
out that hundreds of bytes do not fit -- around eighteen real shaping passes a
snippet, because every probe is a unique substring of a unique line and the
measure cache cannot serve one. The full-line measurement the early-out already
takes gives an advance per byte, so the fitting length is a division and the
search only has to confirm it: `sidebar.snippet_measures` reads 6 per snippet
against `snippets_trimmed`. The guarantee is unchanged -- a cut is only accepted
once it has been *measured* to fit -- and `TextUtilTests` checks the three trims
against a linear reference over every code point boundary, under a measurer
whose per-character widths vary by a factor of ten, which is what makes a wrong
estimate cost probes rather than correctness.

### Where an idle frame goes now

Real session, 400-note library, 200 KB note open, live pane, both panels, 60
frames. `shell.present` is excluded: with vsync on it is the refresh interval
minus the work, and it belongs to the display.

| scope | per frame |
|---|---:|
| `shell.sidebar` | 0.257 ms |
| `page.draw` | 0.225 ms |
| `shell.title_bar` | 0.068 ms |
| `shell.right_panel` | 0.047 ms |
| `shell.content` (self) | 0.051 ms |

Frame work p50 0.70 ms, p95 0.84 ms against a 16.7 ms refresh. The shape is the
point: the sidebar and the page cost about the same, and nothing else is close
to either. Before this pass the right panel outranked the page by 2-3x.

### Resolved: a measure-cache key hashed uninitialised padding

Not a timing, but it belongs here because it is about an instrument. The
`98.5%` measure-cache hit rate reported above was measured through a key built
like this:

```cpp
const struct { FontFamily family; bool strong, italic; float size, scale; } fields {...};
return hashBytes(key, &fields, sizeof(fields));
```

That struct is sixteen bytes with two of them padding, and aggregate
initialisation leaves padding *indeterminate* -- so two of the bytes in every
key were whatever the stack held. gcc at -O2 zeroes them, which is why the hit
rate is a real number rather than a symptom; it is still reading uninitialised
memory, and the failure mode when a compiler stops being kind is the same run
hashing to two keys depending on what ran before it. A hit rate that collapses
with no code change and nothing to point at.

Packed into a twelve-byte array with every byte written. The two other raw-byte
hashes -- `DocumentLayout::Flags` and `TypeMetrics`, both cache keys -- are
padding-free as they stand but only by accident of their current fields, and now
carry a `static_assert` saying so.

**The general rule:** never hash a struct by its bytes. Hash the fields, or pack
them somewhere with no padding to reason about.

## The sixth pass: a UI review, and the second renderer's frame

This pass started as a UI/UX consistency review rather than a performance one:
put the same note on screen in the live pane and the reading pane, take a
screenshot of each, and look at what differs. It found six drawing divergences
(`docs/tech-debt.md`, TD-9) and one number that made no sense.

### Resolved: the reading pane re-wrapped every visible block, every frame

The instrument here was a counter, not a timer.

One 675-byte note, 1600x1000, both panels, 60 frames, the same note in the same
window in each pane:

| | `render.text_measure_calls` | `shell.content` self |
|---|---:|---:|
| live pane | 3,266 | 0.053 ms/frame |
| reading pane | 21,339 | 0.265 ms/frame |
| split pane | 20,732 | 0.531 ms/frame |

Six and a half times the measurements for the same note. Twenty blocks, sixty
frames: 17 measurements per block per frame, on a note where the longest
paragraph is two lines.

Two things were doing it, both in `drawReadingPane`'s per-block loop:

* `blockTextHeight` re-ran `measureInlineLines`, which walks every word of the
  block and measures each one, to recover the height the block occupies. The
  layout memo had already computed that height in `buildViewerLayout` -- that
  is what the memo is for -- and thrown it away, keeping only the running `top`
  of each block.
* the same helper called `inlineRuns` to get the runs it was measuring, while
  the draw had already built them for itself. So the runs -- a vector, and two
  `std::string`s per inline -- were built twice per visible block per frame.

The memo records `bodyHeight` per block now: the height of the block's own body,
which is the one number both passes need and the one the draw was recomputing.
Tables are in it too, because measuring a table measures every cell in it. And
`blockTextHeight` takes the runs it is measuring instead of rebuilding them.

| | before | after |
|---|---:|---:|
| reading pane `text_measure_calls` | 21,339 | 11,674 |
| split pane `text_measure_calls` | 20,732 | 11,367 |

45% fewer, with all eight session screenshots byte-identical and every other
deterministic counter unchanged -- which is the proof that what came out was
duplicate work rather than work.

**What is left is the draw itself,** and it was filed as TD-12: `drawInlineRuns`
measures each word as it places it, because the reading pane has no equivalent
of `doc::Layout`'s per-block run cache. The remaining 11,674 are 99.1% cache
hits, so they are hash-and-probe rather than shaping, and the pane makes its
budget comfortably. The fix is TD-9, not a second cache -- and the seventh pass
below is TD-9.

### Why the harness could not have found this

Every number above came from a real headless session and a counter. The perf
harness never touches `ReadingPane.cpp` -- it stops at `doc::` and `library::`,
as this file says of the fifth pass -- and no *timer* would have made the case
either: 0.265 ms a frame is inside every budget in the file. What said "this is
wrong" was two counters that should have been the same order of magnitude and
were not.

The rule from the third pass stands unchanged, and this is another instance of
it: when you add code to a hot path, add the counter as well as the timer,
because a timing says how long the work took and only a counter says whether it
should have happened at all. The same rule caught the scroll relayout that was
70% of every frame while passing every budget.

## The seventh pass: one renderer instead of two

The sixth pass ended by filing the reading pane's remaining cost as TD-12 and
saying "the fix is TD-9, not a second cache". This is TD-9.

### Resolved: the reading pane was a second renderer for the same Markdown

`src/app/ReadingPane.cpp` parsed the open note through md4c a second time and
drew it with its own geometry: its own indent step, its own quote gutter, its
own callout box, its own task checkbox, its own code block, its own table, its
own image scaling. Every typographic decision had to be made twice, and a
screenshot of one note against the live surface had already found six places
where the two had drifted.

It is `PageView` now, with three flags off. `setReadOnly(true)` withdraws the
caret, the hover gutter and the selection toolbar -- all three of which were
already conditional on focus or on the pointer -- and tells the layout there is
no caret, which is what keeps a block's markers hidden. Nothing else about the
two panes differs.

**The measurement that says so is a `cmp`.** The same note, the same window, in
each pane:

```
$ cmp show-live.png show-viewer.png
differing row bands: [(1078, 1091)]
```

Fourteen rows, and they are the status bar: `Live` against `Reading`. Every
other pixel of the page is identical, which is a stronger statement than any
list of fixed divergences could be -- and it is now a regression test anyone can
run in two commands.

The three things the merge had to *gain* before it could happen, because the
reading pane could do them and the live surface could not:

* **Images.** `doc::Layout` reserves a box under the block that named the
  picture (`BlockLayout::images`), measured through a `Metrics::measureImage`
  hook the way `measureComplex` already worked for tables -- so the page asks
  for a box the way it asks whether a wikilink resolves, and every position
  query below that block is right without a second pass. The alt text became a
  role of its own, `TextRole::ImageAlt`: an underlined accent-coloured line
  above every picture reads as a stray link, the same words muted read as the
  caption they are. `layout.images_measured` is the counter; on an idle frame it
  is zero.
* **Anchors.** `PageView::anchorScroll` builds the note's heading and footnote
  anchors from the block partition it already has, once per buffer. The reading
  pane kept this in a private map, which is why the *live* surface could not
  follow an in-note `[#heading]` link at all.
* **Footnotes**, and this one turned out to be a bug in both panes. md4c drops a
  footnote definition that nothing refers to, and a `Complex` block is parsed on
  its own -- so a definition came back empty and fell through to the raw-source
  fallback. Both surfaces showed the author's `[^label]: ...` in grey monospace.
  Parsing it behind a synthetic reference to its own label is what makes md4c
  keep it. And `doc::InlineScan` learned `[^label]` as a link to `#fn-label`, so
  a reference in the middle of a sentence is now clickable on both surfaces
  where it used to be four literal characters on one of them.

### What the second renderer cost

A real headless session, 60 frames, the 7.2 KB elements fixture, reading pane,
interleaved against the commit before the merge:

| | before | after |
|---|---:|---:|
| `render.text_measure_calls` | 32,579 | 3,728 |
| `markdown.parse_bytes` | 7,262 | 981 |
| `markdown.blocks_produced` | 112 | 7 |
| `shell.content` self, 60 frames | 22.0-50.4 ms | 1.4-2.2 ms |

**Eight and a half times fewer text measurements, and fifteen to twenty-five
times less time.** The measure calls are TD-12 answered: the pane draws
`doc::Layout`'s cached runs -- position, width and style, already decided --
instead of walking every word of every visible block and measuring it to place
it. The parse figures are the other half: md4c now sees only the blocks the
scanner hands it (nine of them, cached by their own bytes) rather than the whole
note on every buffer change.

`page.runs_drawn` is 27,720 over the run in both panes, and `layout.*` is
identical between them, because they are the same code.

### Why this was a merge rather than a cache

TD-12's own entry named the alternative: give the reading pane a per-block run
cache of its own, keyed on (block, textWidth, fontScale). That is most of what
`doc::Layout` already is, built a second time inside the file the merge was
going to delete -- and it would have fixed the *speed* half while leaving every
drawing decision still made twice. The six divergences the sixth pass found were
all in the half a cache would not have touched.

### Resolved: the layout walked the filesystem once per picture per relaid block

Found by pointing the new instrument at the new feature: a 200-picture note,
`MICRONOTES_TRACE_FRAMES` off, counters on.

```
page.layout           70.4 ms over 60 frames, max 46.9 ms
shell.content         83.1 ms over 60 frames, max 49.8 ms
layout.images_measured   400
```

A 47 ms first frame -- three dropped frames to open a note -- for a layout whose
text is 14 KB. `Metrics::measureImage` asks the shell where a target lands on
disk, and the shell answered with `AttachmentService::resolveManaged`, which is
`platform::normalizeInsideRoot`, which is `std::filesystem::weakly_canonical` of
the library root **and** of the candidate: a `stat` per path component of each,
twice, per picture, per relaid block. Two hundred pictures is about 3,200
syscalls to lay a note out, and a resize pays it again.

`ImageCache` was already memoising the expensive-looking half -- decoding and
uploading the texture, which for a note showing one file two hundred times
happens once. The cheap-looking half was the cost.

The resolution is memoised on the runtime now, keyed by the target as written
and dropped when the library root moves:

| 200 pictures, 60 frames | before | after |
|---|---:|---:|
| `page.layout` (inclusive) | 70.4 ms | 2.5 ms |
| worst frame | 46.9 ms | 1.9 ms |
| `shell.content` | 83.1 ms | 4.7 ms |
| `image.paths_resolved` | -- | 1 |
| `image.paths_reused` | -- | 579 |

**Twenty-five times, and the frame that matters by a factor of twenty-five as
well.** `image.paths_resolved` should be the number of distinct targets in a
note and nothing like the number of times they are laid out; it is the counter
to watch if the resolution ever moves back inside the loop.

The general lesson is the third pass's, again: the counter is what said this was
wrong. `page.layout`'s *timing* on the second frame is 0.04 ms and every budget
in the file was green -- it was `layout.images_measured` reading 400 for a note
with 200 pictures in it, against a `page.layout` max of 47 ms, that did not add
up.

### Resolved: two caches made room by throwing away what they were using

TD-15 said both were "bounded, and that is why it is an entry rather than a
fix". Measuring them said otherwise, twice.

**The md4c parse cache had a hit rate of zero above 64 blocks.** It was
`if(size() > 64) clear()`, and a relayout touches every `Complex` block in the
note — so a note with 120 tables filled the cache, cleared it at 64, refilled
it, and cleared it again, every pass:

```
PROBE complex blocks=120  first=120  second=120  third=120
```

Three full passes over the same unchanged note, 120 md4c parses each. The cap
was not too small; it was the wrong shape. A relayout's working set *is* the
note, so any cap below the note's own size is not a cache.

Its live set is enumerable — the `Complex` blocks of the buffer on screen — so
it gets the rule `doc::DocumentLayout` already uses for the block layouts it
mirrors: keep one generation of the document, sweep when the cache runs past it.
`sweepComplexCache` runs once a frame and costs one comparison until the cache
has grown past what the last sweep found live. The same probe now reads
`120 / 0 / 0`, and opening a second note drops the first note's hundred rather
than accumulating them.

**The image cache re-laid out the whole document, every frame, above 512
pictures.** `clear()` moves `generation()`, and `generation()` is an input to
every block layout holding an image — so filling the cache cleared it, which
invalidated the note, which re-measured every picture, which filled it again:

| 600 distinct pictures, 60 frames | before | after |
|---|---:|---:|
| `layout.blocks_relaid` | 72,180 | 2,406 |
| `layout.images_measured` | 36,000 | 1,200 |
| `page.layout` (inclusive) | 1,031 ms | 112 ms |

72,180 is the 1,203-block note laid out sixty times: **once per frame, for
ever**, at 17 ms a frame. Not a slope — a cliff at 512 pictures.

Two changes, and the first is the one that matters. An image's **size** is what
the layout reserves a box from, and a file decodes to the same size every time —
so the size is learned once and kept, and evicting a texture leaves it behind.
`generation()` now moves when a size becomes *newly* known and at no other
moment, which is what makes eviction cost a decode instead of a relayout. A note
whose pictures do not fit shows it:

```
image.textures_loaded 49   image.textures_evicted 33   layout.blocks_relaid 102
```

Thirty-three evictions and the note is still laid out twice, not thirty-five
times.

The second is the budget: **bytes, not entries**. The 512-entry cap meant 69 MB
for a note of 240x140 diagrams and 24 GB for one of phone photographs — the same
number standing for two things four orders of magnitude apart, because it counted
the wrong thing. It is 192 MB of texture now, evicted least-recently-used, which
is the policy `render::TextTextureCache` already settled on for the same reason:
where a cache's live set cannot be enumerated from where it is asked, recency is
the best available guess. Where it *can* be — the parse cache above, the block
layouts — the sweep is better, and the two answers are not in tension.

`peak_rss` on both fixtures is marginally *lower* than before, because
re-parsing and re-decoding churn more than holding the results does.


## The eighth pass: the save nobody had measured

The fifth pass found 1.1 ms of `fsync` on every keystroke by looking for what
the instruments were not pointed at. This pass is the same question asked again,
one layer up, and the answer was larger: **the harness had no lane for saving at
all.** Every budget in `tools/PerfMain.cpp` was a layout or a paint, so the
autosave -- which runs 1.2 s after the last keystroke and again every second
while typing continues -- could grow a whole-library tree walk and a whole-file
read and the whole suite would stay green.

It had. Writing the lane first, before touching a line of the save path, is what
made the rest of the pass arithmetic instead of opinion:

```
save.autosave_note   18,713 us median   59,020 us worst   39,350 allocs   5,872 KB
save.durable_write_200kb  1,363 us median                       26 allocs      2.5 KB
```

One autosave of a 200 KB note in the existing 1,000-note fixture cost **eighteen
milliseconds and thirty-nine thousand allocations**, against 1.4 ms for the
durable write of the same bytes. Seventeen of those milliseconds were overhead
around the write, once a second, against a 2 ms frame budget.

The lane is wall-clock rather than `CLOCK_PROCESS_CPUTIME_ID` like every
scenario above it, and that is not a lapse: a durable write is two `fsync`
barriers, and a barrier is time the process spends *blocked* rather than
running. On CPU time the same save reads as about 60 us of work, which is a
precise measurement of the wrong thing. The allocation counts beside the medians
stay the machine-independent half, and on this pass they carried the argument.

### Resolved: a save rescanned the whole library to discover what it had written

`saveSelectedNote` ended in `refreshLibrary()`: a recursive walk of the tree, a
stat per note, a read of every row in the SQLite index to compare against, and
then every memo the note list had built -- the list, the folder counts, the tag
list, the id map -- thrown away and rebuilt on next use.

`refreshChangedFiles` exists to *discover* what changed and it earns its cost
when nobody can say. A save can: it knows the one path it just wrote, and it is
still holding the bytes. `LibraryIndex::refreshFile` re-indexes a named file --
a stat, and only if the stat moved, one read and one transaction --  and
`refreshWrittenFile` skips even the read, taking the front matter and body from
the caller. An index on `notes(path)` (schema 4) is what makes the by-path
lookup a lookup rather than a scan of the table per save.

The other half was `FileRefresh::listFieldsChanged`. The note list is built from
five fields: id, path, title, tags, icon. An ordinary save moves none of them --
it moves the body -- so the sidebar, the folder counts and the tag list do not
have to be rebuilt at all. The library revision still moves, because backlinks
and search results *do* change with a body; but `rowsWritten` keeps even that
from moving when a refresh finds nothing changed, which is what the watcher's
echo of the app's own write looks like.

### Resolved: three separate readers re-read the open note on every save

`AppState::selectedNote()` opened the note, read all of it, parsed its front
matter and handed back a copy of the body -- and it was how everything asked
about the open note. The page header wanted its title. The right-hand panel
wanted its tags, for a row of chips. The save itself wanted its `id`. All three
memoised on the library revision, and the library revision moved on every save.

So a 200 KB note was read and parsed three times per autosave, to recover facts
that had not changed and that the app had just written. `AppState::OpenNote` is
the record: front matter, path, and the file's signature, read once per note.
`app_state.open_note_reads` is the counter that keeps it honest -- two for a
whole harness run, where it used to be three per save.

### The result

Interleaved through `tools/perf-compare.py`, counters first because they are
exact:

| counter | before | after |
|---|---:|---:|
| `library.index_files_scanned` | 22,020 | 5,003 |
| `library.directory_entries_visited` | 22,086 | 5,018 |
| `library.note_rows_selected` | 18,018 | 2,002 |
| `library.index_refresh_calls` | 22 | 5 |
| `app_state.open_note_reads` | 3 per save | 2 per run |

and the lane, per autosave, over the three commits that got there:

| | start | one file, not the library | no copy of the note |
|---|---:|---:|---:|
| `save.autosave_note` | 18,713 us | 4,386 us | **3,627 us** |
| allocations | 39,350 | 62 | **61** |
| allocated | 5,872 KB | 205 KB | **4.6 KB** |
| largest allocation | 256 KB | 256 KB | **0.3 KB** |

The last column is the one that says the shape is now right: **a save allocates
no copy of the note at all.** The 205 KB was the body being moved into the
index's row struct and then copied again inside sqlite, because every column was
bound `SQLITE_TRANSIENT`. It is lent instead -- `SQLITE_STATIC` over the save's
own buffer, which outlives the transaction.

`save.durable_write_200kb` is 1,141 us on this filesystem and did not move. A
save is now 3.2 barrier-pairs' worth of work where it was 13.8.

### What the numbers could not say

Three of the findings in this pass were not performance findings, and no
instrument would ever have reported them. They came out of reading the save path
closely enough to make it fast:

- **Nothing checked what it was overwriting.** A note edited in another program,
  delivered by a sync daemon or replaced by a `git checkout` was destroyed by
  the next autosave. Worse than destroyed: the save re-read the *changed* file
  for its front matter and wrote the stale buffer under it, so the external body
  was gone and its header was not.
- **`loadSelectedIntoEditor` never consulted the recovery store,** and it is the
  *startup* path -- the one case crash recovery exists for. The two hundred lines
  of durable-write machinery the fifth pass built worked only if you happened to
  click the note's name again after restarting.
- **The first save of a note another tool wrote made it vanish.** Such a note is
  filed under an id derived from its path; the save gave it a permanent one and
  left the selection, its tab and the favorites pointing at the id it no longer
  had.

The lesson is the fifth pass's, one turn further round: the instruments answer
the questions they were aimed at. `docs/tech-debt.md` TD-16 and TD-17 are what
this pass found and chose not to pay.

### The watcher, and why it is not a timer

"A file changed on disk should be reloaded" was answered on window focus first,
because that is where the app already refreshed. It covers coming back from
another editor and nothing else: a `git pull` in a terminal on the next monitor,
or a sync daemon delivering a change, happens while the window is sitting there
with focus.

The cheap answer would have been a one-second `stat` of the open file -- ten
lines, and *worse on the priority order this project is built on*. A timer means
waking a sleeping process once a second, forever, to be told nothing happened.
`platform::DirectoryWatcher` is one inotify descriptor, a watch per directory,
and a thread asleep in `poll`; it costs nothing at all until something changes.
Measured on a real session, reading `/proc/<pid>/stat`:

```
cpu ticks after 3s idle:                   user=5 sys=2
cpu ticks after the external change:       user=7 sys=2
cpu ticks after 3s more idle:              user=7 sys=2
```

Zero while idle, two ticks to notice and repaint, zero again. `cmp` of the
framebuffer before and after says the window drew the new text with no input and
no focus change.

Two things about it took a real session to get right. The mask is
`IN_CLOSE_WRITE | IN_MOVED_TO`, not `IN_MODIFY`: every careful writer -- this
app included -- saves by writing a temp and renaming it into place, so an atomic
save arrives as a *move* and never as a write at all. And the app's own writes
come back through the watcher with nothing filtering them out, because nothing
needs to: a re-index whose stat matches its row writes no rows, and a reload
whose signature agrees does not happen. Comparing the disk is what makes echo
suppression free, where remembering who wrote what would have been a second
piece of state to get wrong.

## The ninth pass: the two things a keystroke did that nobody had counted

The eighth pass ended on a rule: *a lane the harness does not have is a budget
nothing enforces*. This pass is that rule applied twice more, in the same place
-- a keystroke -- and the two findings it turned up were each larger than the
layout work every existing budget measures.

Every edit lane here measures `doc::Layout` directly. That is the right thing
for a core benchmark and it is also the reason both of these were invisible: the
harness has no shell in it, so what a keystroke costs *the surfaces around the
page* had never been measured at all. Both were found by asking the plain
question "what else runs when the editor's revision moves?" and then timing the
answers.

Measured on a 200 KB note, which is what the rest of the file uses, against a
keystroke whose own layout update is **14 us**:

| what runs on a keystroke | before | after |
|---|---:|---:|
| the outline panel's rebuild | 239.9 us | **~18 us** |
| the status bar's word count | 247.4 us | **~0** (3 bytes read) |

### Resolved: the outline panel rescanned the note on every keystroke

`outlineFor` is memoised on the editor's revision, and the comment above it said
"rebuilt only when the buffer has moved". Both true, and together they hide the
problem: the buffer moves on every character. Each rebuild called `outlineOf`,
which derives the block partition from scratch -- a pass over every byte plus a
fresh `vector<SourceBlock>`, 199 us of the 240 us.

The partition already existed. The live page splices one incrementally during
its own update, and `app::editorBlocks` is the borrow that hands it over -- the
block edits in `doc/Edits.h` were moved onto it for exactly this reason, and the
outline was the caller left behind.

**The interesting part is why the fix did not work at first.** `blocksAt`
answers only for the revision the partition was built from, which is what makes
the borrow safe. The right panel was drawn *before* the content, so it asked one
revision early, was correctly refused, and rescanned every time -- the borrow was
in the code and never once hit. Drawing the panel after the content is the whole
difference, and nothing could see it: identical pixels in all three panes, a
green suite, and the only evidence a counter in a session nobody was running.

That is now guarded at both halves, because they break independently:
`shell_outline_borrows_the_partition_the_live_page_already_spliced` pins the
borrow through the counters, and
`architecture_the_right_panel_is_drawn_after_the_content` pins the order. The
first still passes with the order wrong, which is why the second exists.

### Resolved: the status bar recounted the whole note on every keystroke

The same shape, one file over. The word count was memoised on the buffer's
revision, and that memo was added by an earlier pass to stop it running per
*frame* -- a scroll, a hover and a window focus all draw one and none of them
touches the text. It did that. What a revision key cannot do is stop the work
running per *keystroke*, because that is precisely when the revision moves.

The counters beside it read exactly right throughout: `status.word_counts`
against `status.word_counts_reused` was built to catch the per-frame version,
and it did, and it had nothing to say about the per-keystroke one underneath.
**A counter answers the question it was written to ask.**

`MarkdownEditor` carries the count across each edit now. The window is the
replaced span plus one byte, which is the exact extent of what an edit can
change: positions below the splice are decided by two bytes that did not move,
the position at the far end holds an unchanged byte but its *predecessor* moved,
and past that both deciding bytes come from the untouched suffix.
`editor.word_count_bytes_scanned` reads **3 bytes per keystroke** where a
recount is 204,832.

The first version of that window widened to the nearest whitespace on each side.
Also exact, and O(document) on a buffer with no whitespace in it -- a minified
file, a base64 blob -- which is the exact shape the change existed to remove. It
was caught by the undo tests, which drive a 12 MB run of `x`: the suite went
from 3.0 s to 9.7 s and stayed green, because the answers were right and only
the cost was wrong. **A correctness suite that says nothing about cost will let
an O(n) fix for an O(n) problem straight through.**

### Resolved: reading a note spent longer checking the path than reading it

`platform::normalizeInsideRoot` is the containment check every path into the
library goes through, and it canonicalized *both* sides on every call --
a `stat` per component. The root's half is the same answer every time, because a
library's root does not move while it is open.

| root depth | `Library::loadNote` before | after |
|---|---:|---:|
| `/tmp/mn-probe` (2 components) | 12.28 us | **5.81 us** |
| `~/.../Documents/notes` (6 components) | 23.83 us | **13.43 us** |

Reading a note was spending more time deciding the path was allowed than
reading, parsing and splitting the file put together -- and the saving grows
with the depth of the root, so it is worst on the paths people actually keep
notes in and least visible on the shallow fixture the harness uses. Through the
harness, `library_index.refresh.read_file` over 1,001 notes went 5.603 ms ->
4.693 ms as a minimum of eight interleaved rounds.

`platform::SafeRoot` resolves the root once and the candidate per call, which
puts the invariant in the type rather than in seventeen call sites' discipline.
The check also had no test at all, which is a poor state for the one function
here whose failure is a security failure; there are two now, and they pin the
case a prefix comparison gets wrong (`/…/library-backup` shares the root's
characters and is not inside it).

### Resolved: a search copied the library to look at it

`collectRows` read every result row's `body` column into a `std::string`. The
result set is capped at 200 rows, the body is the largest thing in a row by
three orders of magnitude, and what the copy is *for* is finding at most three
lines of one note.

Search also had no lane, so `searchBudgets` is new: a query that matches
everything, one that matches nothing and so falls through fts5 to the `LIKE`
scan over every row, and a titles-only control that shares the whole path except
the bodies.

| scenario | allocations | bytes |
|---|---:|---:|
| `search.query_hits_everything` | 3,173 -> **2,972** | 781.9 KB -> **485.7 KB** |
| `search.query_titles_only` | 2,973 -> **2,772** | 707.3 KB -> **413.6 KB** |

The medians sit inside the run-to-run band with no counter behind them, so the
allocations are the result and the timings are not. The bytes fall by 38% on a
fixture whose notes are 1.5 KB, and the saving is one body per row -- so it
grows with the size of the notes rather than staying put.

### Resolved: the undo history was bounded in steps, on a snapshot of the buffer

An undo snapshot is the whole buffer and the only ceiling was a count of 100. On
a 200 KB note that is **19.6 MB of undo for one open note**, against a
whole-process peak RSS of about 30 MB. The editor's history was the largest
thing in the process.

`editor_undo_history_is_bounded` was the guard and could not have failed: it
asserted `undoBytes() <= 32 MB` while driving a **4 KB** buffer, so what it
measured was 400 KB. **A bound expressed in bytes has to be driven at a size
where bytes are what runs out.** There are two ceilings now -- the count for
small notes, 8 MB for large ones, and a floor of 8 steps whatever the size --
and the tests drive them at 200 KB and at 12 MB.

### Resolved: a step was still a copy of the whole note, and a small note paid for it

Capping the bytes stopped the history being the largest thing in the process. It
did not make a step cost what the *edit* cost, and the ratio a ceiling hides is
the one a small note feels: a long session on a **2 KB** note retained 214.6 KB,
**107 times the note it belonged to**, and no ceiling was anywhere near tripping.
The count governed, 100 steps was the answer, and 100 copies of a small note is
a hundred copies of a small note.

The harness could not see it, for a reason worth naming: every lane in the file
measures a *rate* -- what a keystroke, a frame or a save costs -- and undo is a
**ceiling**, a thing held for as long as the note is open. The only number of
that shape in the harness was `peak_rss`, which covers a 1,000-note fixture and
an SQLite index too and so cannot attribute anything to the editor. So the undo
lane is new, and it reports retained bytes *next to the size of the note they
belong to*, because the ratio is the finding and the absolute figure is not.

A step is the splice that reverses the edit now -- an offset, the bytes that came
out, the length of the bytes that went in -- so it costs the *edit* rather than
the document, and the same record type describes its own inverse, which is what
makes undo and redo one function reading different stacks.

| | before | after |
|---|---:|---:|
| 2 KB note, 200 edits | 214.6 KB (**107x** the note) | **8.5 KB** (4.25x) |
| 200 KB note, 400 edits | 8,014.8 KB, 40 steps | **8.5 KB, 100 steps** |
| one Ctrl+Z on a 200 KB note | 97 us, 1 alloc, 200.3 KB | **3 us, 0 allocs, 0.3 KB** |

Three things fell out that were not the point:

- **the large note now keeps its full 100 steps** and still retains less than the
  small note used to. The byte ceiling was trading depth away to pay for copies.
- **undo went through `splice`**, so it carries the word count across the edit
  instead of recounting the note, and `lastChange` reports the span that
  actually moved instead of the whole buffer. Ctrl+Z used to tell the live
  layout every byte had changed, which is the one claim that bounds nothing.
- **`snapshot` compared `undo_.back().text != text_`** before pushing, to avoid a
  duplicate. That was a whole-buffer `memcmp` on every non-coalesced keystroke,
  and it went away with the snapshots: the five edit sites already reject a
  no-op, so every step is a real change by construction.

The one case that still retains bytes is the honest one -- deleting text, where
the history holds the only remaining copy of what came out -- and the byte
ceiling is now *for* that case rather than for ordinary typing. 80 whole-note
replacements of a 200 KB note sit at 8,002.8 KB and keep 40 steps.

What coalescing cost is worth recording, because it was the one thing a snapshot
history got for free: folding a keystroke into the open step used to mean
*declining to push*, since the pre-edit buffer was already there. A splice has to
be widened, and typing, Backspace and Delete widen it in three different
directions -- tail, head, and tail-of-the-removed-text respectively. Each branch
re-checks the shape of the step it is folding into rather than trusting the group
bookkeeping, and returns false to open a fresh step when it cannot prove the two
are one splice: an extra Ctrl+Z is a cost, guessing wrong corrupts the buffer the
user gets back.

### What this pass says about the instruments

Three of the five findings above were invisible to every lane in the harness,
and the other two were invisible to the lane that was supposed to see them.
The pattern is worth naming, because it is the same one each time:

- **the outline and the word count** were memoised on the editor's revision, and
  both memos worked. What neither could do is make the underlying work cheap on
  the one event that always invalidates them. A memo turns "per frame" into "per
  edit"; only an incremental algorithm turns "per edit" into "per edit's size".
- **the undo bound** and **the word count's own counters** were both real
  instruments reading correct values for the question they were written to ask,
  next to a much bigger version of the same question nobody had asked. The undo
  bound then did it a second time: capping the bytes answered "is this history
  unbounded" correctly and said nothing about a step costing the whole document,
  which is the question a 2 KB note needed asked. **A ceiling hides a ratio.**

The harness now has a search lane. It still has no lane for **a keystroke
through the shell** -- see `TD-19` -- which is what both of the first two
findings would have needed, and is the largest hole left in it.

## The instrument itself: a capture cost more than the frames it measured

Not an app finding. `session-compare.sh` is built out of `--screenshot` runs,
and `docs/performance.md` calls the pixel `cmp` "the cheapest proof there is"
that a paint change is unobservable — so what a capture costs decides how often
anybody takes one.

A capture drew sixty frames with an `SDL_Delay(8)` between them, and took
**1.10 s**. `TD-21` read that as a fixed ~500 ms floor of sleeping and proposed
a readiness signal — `SDL_EVENT_WINDOW_EXPOSED` plus a settled size — to end the
wait as soon as the window was really up.

Two things were wrong with that, and both only showed up when measured.

**The sixty frames are the sample, not the wait.** Every table in this file is
"Release, headless, 60 frames" with the per-frame figures divided by it. A
capture that stopped as soon as the window was up drew three frames — quick, and
measuring nothing. An idle-frame regression is exactly what this instrument
exists to catch; the sidebar rebuilding per frame and the fold predicate asked
per block per frame are both in the list above, and over three frames neither
shows.

**Removing the sleep buys exactly zero.** Vsync is on, so `SDL_RenderPresent`
blocks until the next refresh either way: a frame that slept 8 ms first simply
waits 8 ms less in the present. 1.10 s with the sleep, 1.09 s without it, with
`frame.present_micros` moving from 503,390 to 982,278 to absorb the difference
exactly.

| | wall clock | `frame.present_micros` | frames sampled |
| --- | ---: | ---: | ---: |
| sixty frames, `SDL_Delay(8)` | 1.10 s | 503,390 | 60 |
| sixty frames, no delay | 1.09 s | 982,278 | 60 |
| stop when the window is up | 0.16 s | 3 frames' worth | **3** |
| **sixty frames, vsync off** | **0.36 s** | **241,688** | 60 |

The fix is the one neither the entry nor the sleep pointed at: **a capture has
no viewer, so it should not be paced to a display.** The app keeps vsync on,
because a present that beats the display is a frame thrown away; the capture
turns it off for the frames nobody will see. 1.10 s to 0.36 s, the sixty-frame
sample intact, and the pixels byte-identical in all three pane modes.

The reading worth keeping is the second one. The sleep was the obvious
suspect — it is a literal `SDL_Delay` in the hot loop, and it is what the debt
entry named — and it cost nothing at all, because something further down was
already absorbing it. **A wait inside a paced loop is free until you unpace the
loop.**


### The shell lane, and the first thing it found

`TD-19` said the harness had no lane for a keystroke through the *shell*: every
edit lane drove `doc::Layout` directly, so anything memoised on
`ui.editor.revision()` — which is by construction recomputed on every keystroke,
with the memo making it look handled — was invisible to `run-checks.sh perf`.
Two findings of exactly that shape shipped and were found by reading code: the
outline panel rebuilding its block partition per keystroke (240 us) and the
status bar recounting the whole note (247 us).

The lane exists now. It drives a real `UiRuntime` — a real editor, a real live
page, the real right-hand panel — over the real faces, and stops short of the
paint: no window, no textures, no present. Everything above the paint is where
all three findings were. It asks in the order `drawApp` does, because the
outline borrows the partition the live page splices and `blocksAt` refuses to
hand over one from a revision the layout has not reached; a lane that asked in
the other order would measure the scan and call it the cost of the panel. That
ordering is asserted rather than assumed — the lane fails if the outline scanned
more often than it borrowed.

On a 200 KB note, on an idle machine:

| | median |
|---|---:|
| `shell.edit` — the buffer splice alone | 1 us |
| `shell.status_bar` — the word and character counts | 1 us |
| `shell.live_page` — layout over the real faces | 16 us |
| `shell.outline_panel` — edit, layout, then the outline | 29 us |
| `shell.keystroke` — all of the above, in frame order | **39 us** |
| `shell.raw_pane_rewrap` — edit, then the raw pane's rows | **70,721 us** |

The first five are the two ninth-pass findings staying fixed. The sixth is the
lane earning itself on the first run.

**`TD-14`'s number was off by two orders of magnitude.** The entry recorded the
raw pane's whole-note rewrap at 807 us per keystroke; over a real face it is
**70 ms**, and it is 1,800 times the cost of the rest of the keystroke put
together. The 807 us was measured against a fixed-advance stand-in, which is
what every lane here used before the font lane existed, and a fixed advance is
precisely the assumption this code does not get to make.

The reason is in `editor::softWrap`'s break search. Finding where a line breaks
is a binary search over the row's *prefixes*, and every probe is a distinct
string: `measure(text[pos..mid])` for seven or eight different `mid` per row,
several thousand rows, none of them a string anything has measured before. So
the measure cache cannot help — and worse, each miss *inserts*, and the cache is
direct-mapped, so a single rewrap evicts thousands of the entries the page
layout depends on. The pane does not merely cost 70 ms; it takes the shaping
cache down with it on the way.

That is now a budget rather than an anecdote (`shell.raw_pane_rewrap`, ceilinged
loosely because shaping moves with the machine), and it is the number `TD-14`'s
decision should be taken against.


### Resolved: the index carried a second copy of every note

`notes.body` and `notes_fts.body` both held the whole text of every note, so the
index was two and a half times the size of the library it indexed -- and every
save wrote the body twice for the same reason.

| 1,000 notes, 8 sections each | on disk |
|---|---:|
| the Markdown itself | 10.3 MB |
| index, `fts5(title, body, path)` | **27.2 MB** |
| index, `fts5(..., content='', contentless_delete=1)` | **15.5 MB** |

The copy was never read. The only thing asked of the full-text table is *which
rowids match*: `search` selects `notes.id`, `notes.path`, `notes.title` and
`notes.body` through the join, and the snippets are built in C++ from that
`body`. A contentless table stores the terms and not the text, which is exactly
the half that was doing the work.

**What made this a decision rather than a change** is deleting a row. A
contentless fts5 table could not have one deleted until SQLite 3.43 added
`contentless_delete=1`, and deleting a row is what every save does -- the writer
removes the note's entry by rowid and inserts the new one. The other shape,
`content='notes'`, works on any fts5 and stores no copy either, but its delete
takes the *old* column values, which means reading the old body back out of
SQLite on the autosave path: the exact whole-note read the eighth pass went to
some trouble to remove.

micronotes links the **system** SQLite, so 3.43 cannot be assumed. The create is
attempted and the old stored-content table is the fallback, which is what every
build wrote until now. Nothing above `migrate` knows which of the two it got:
insert by rowid, delete by rowid, delete all, and a MATCH scoped to one column
or to none all mean the same thing on both, which is what makes the fallback a
fallback and not a second mode. `LibraryIndex::ftsStoresBodies()` is the one
question that can tell them apart, and it reads the table's own DDL rather than
querying it -- a contentless table still declares every column and answers NULL
for it, so a `SELECT body FROM notes_fts` prepares and steps happily on both.


### Resolved: the raw pane shaped every prefix of every line to find its breaks

The lane above measured the raw pane's whole-note rewrap at **70,721 us** per
keystroke -- 1,800 times the cost of the rest of a keystroke through the shell.
It is **720 us** now, and nothing about the wrap changed.

What changed is what a width is. The pane shows the file as a *monospaced grid*,
so a run of ASCII is as wide as it is long: `n` cells, one advance each. It was
asking the font instead, and `editor::softWrap` asks in the worst possible
pattern -- it finds a break by bisecting the row, `measure(text[pos..mid])` for
seven or eight different `mid`, several thousand rows of them. Every probe is a
distinct string, so the measure cache missed on all of them **and inserted**,
evicting thousands of the entries the *page* layout depends on. The pane did not
merely cost 70 ms; it took the shaping cache down with it.

| a 200 KB note, per keystroke | |
|---|---:|
| whole-note rewrap, shaping every prefix | 70,721 us |
| whole-note rewrap, counting cells | **720 us** |
| the rest of the keystroke, for scale | 39 us |

Two guards, because "monospaced" is a claim about a face and not a fact about
one. The advance is checked against a sixteen-character probe -- sixteen rather
than one, because a single glyph measures its *ink* and the last glyph of a
string can overhang its own advance (`W` and `%` in the bundled face are a pixel
wider drawn than they are wide to walk), while over a probe they cancel. A face
that fails the check keeps the old path. And a run carrying any byte above ASCII
is measured rather than counted, because a cell is not what a CJK glyph or an
emoji takes, and being approximately right about the width of a line is not
something a wrap gets to be.

`tools/session-compare.sh` reports the pixels unchanged, which is the part worth
saying: the wrap this produces is the wrap the shaping produced, for every line
the fixture holds.

The rewrap is still the whole note on every keystroke -- `editor::softWrap` has
no incremental form -- and at 720 us against a 2 ms keystroke budget that is now
a thing to know rather than a thing to fix. `shell.raw_pane_rewrap` is the
budget that keeps it that way.


### Resolved: a resize step waited for the display it was already behind

Every frame is drawn from scratch and presented with vsync on. The window is
`SDL_WINDOW_BORDERLESS` with a custom hit test, so a drag on its edge is an
*opaque* resize the window manager performs: the X window grows, and the region
it grew into holds undefined content until micronotes presents. That is what
reads as a flicker to black for the whole drag.

The entry proposed a retained scene texture -- keep the last frame, blit it when
the window is exposed, draw the real one after. **That would not have helped,
and working out why is what found the actual answer.** With vsync on, the blit
takes the next refresh and the real frame takes the one after: the window shows
a *stretched, stale* frame one refresh sooner than it would have shown the
correct one. The wait is the problem, not the absence of something to show.

So the drag is unpaced and everything else is paced. `app/ResizePacing.h` notes
the time of the last resize event and turns vsync off until the drag settles;
`drawApp` settles it once a frame, just before the present, and the SDL call is
made only on the two edges. `frame.pacing_changes` is the instrument that keeps
it honest: two per drag. A number that grows with the frame count would mean
something is calling a resize a frame, and on a typing session with no resize in
it the counter reads 2 -- the startup expose, off and on again -- over 66
presents.

Ten scripted `xdotool windowsize` steps on a 1000x700 window, interleaved:

| `frame.present_micros`, 20 presents | paced | unpaced during the drag |
|---|---:|---:|
| round 1 | 140,212 | **130,785** |
| round 2 | 141,837 | **126,464** |
| round 3 | 145,839 | **130,372** |
| round 4 | 139,414 | **130,412** |

**Read that with its limit attached.** These are on Xvfb, which has no display
to pace to, so what is being measured is whatever pacing SDL's software path
does anyway -- 7.08 ms a present against 6.47 ms, seven per cent, consistent
across four interleaved rounds and with no overlap between the two sets. The
entry's own numbers were taken on a real window and put the present at 10.8 ms
against 2.3 ms of drawing, so on a display that genuinely blocks there is far
more of it to remove; that part is reasoned rather than measured, and it is the
one measurement this machine cannot take.

Tearing during a drag is not a cost worth naming next to a window that has not
been painted at all, and a present that beats the display is a frame thrown
away -- which is why vsync is on the rest of the time, and why the pacing comes
straight back the moment the drag settles.
