# Agent Guide

First-stop operating guide for agents working in this repository.

## Quick Scan

- `micronotes` is a Linux-only Markdown notes app in C++20, CMake, SDL3, SQLite.
- Priority order: **speed, then correctness, then low CPU/memory**.
- Known debt is in `docs/tech-debt.md`, numbered `TD-n`. Read it before deciding
  something is unaccounted for, and add to it rather than leaving a `TODO`.
  Six entries are open: one about selecting inside a block the live scanner
  does not model, one about how far a CFF face can be subsetted without
  writing a CFF writer, one about the wheel over the tab strip, one about
  the command chain being checked by grepping its own source, one about
  the find bar rescanning the whole note per keystroke, and one about autosave
  re-tokenising the whole note into the search index once a second. So
  something that looks unaccounted for elsewhere
  probably is, and the honest answers are to fix it or to open an entry that
  says what it costs and why not.
- `src/core/` is the app-agnostic layer. Read the rule below before touching it.
- The tree is layered and the layers only point one way: **core < doc < library
  < ui < export < app**. `architecture_the_layers_only_point_one_way` checks
  it, because it had already broken -- one include, for one function -- and
  made `ui` and `library` a cycle without anything failing.
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

App-only code stays outside, in the layer that owns the concept:

- `src/doc/` -- the Markdown *document*: the block and inline scanners, the
  incremental layout, the edits, `[[wikilink]]` syntax, what a link target means.
  Laying a block out is three units and they compose in that order:
  `doc/Tokenize.h` turns source bytes into tokens, `doc/Flow.h` turns tokens
  into visual lines, and `doc/LayoutUpdate.cpp` decides which blocks need either.
  `doc/Layout.cpp` is what is left -- styling, staging, placement.
  A block the scanner deliberately does *not* model -- a table, raw HTML, a
  footnote definition -- is parsed by md4c and laid out by `doc/RenderLayout.h`,
  through that same tokenizer and that same flow. It lives here rather than in
  either surface because both surfaces need it: it was written twice, once in
  `app/` with a second line breaker of its own and once in `export/` with no
  line breaker at all, and the copy that could not reach the other set every
  table cell as plain text. Two painters over one layout, again.
- `src/library/` -- the *folder of notes*: the index, front matter, search
  scope, trash, and which note a `[[target]]` resolves to. `NoteCatalog` is the
  library, its index and the memos over both kept in step -- every write to a
  note's file goes through it so that re-indexing what was just written is not
  something a caller can forget. Two units inside it are named rather than
  implied. `library/Trash.h` is the trash: the index format, the name
  reservation and the rule that the index line lands *before* the file moves --
  `Library` keeps only which files a deletion is about. And
  `library/StatePaths.h` is where every path under the per-library state
  directory comes from; `ArchitectureTests` fails on a `".micronotes"` written
  out anywhere under `src/`, because the walk that prunes that directory read it
  from `microcore::kAppDotDir` while all six writers into it named a literal.
- `src/ui/` -- what draws and what models a surface. Everything in here either
  paints, measures, or is state a surface keeps. A helper that only counts
  bytes belongs in `core/util/`; one that takes a `measure` belongs here.
  The drawing itself is a stack of six, each depending only on what is below
  it: `ui/Painter.h` (fill, stroke, rule, surface, row, focus ring),
  `ui/Glyphs.h` (every mark drawn rather than typeset), `ui/TextRenderer.h`
  (typesetting and its two caches), `ui/ImageCache.h` (decoded pictures),
  `ui/Scrollbar.h` (the geometry four askers share, plus `scrollbarReserve` --
  what a list must keep clear at its trailing edge) and `ui/Widgets.h` (the
  compounds a surface assembles itself from). They were one `ui/Draw.h`, which
  meant a file that only measured a string included the picture decoder.
  Include the layer you draw with, not the stack.
  `ui/DocStyle.h` is the odd one and is here on purpose: it turns a
  `doc::RunStyle` into a face and a `doc::TextRole` into ink, and this is the
  lowest layer that can see both a role and a palette -- `doc/` names no colour
  at all, and `app/` and `export/` are both above. Both of those had a copy of
  that table, and the copies had drifted. `ui/DocRuns.h` is there for the same
  reason and one step further on: it is the *whole* run loop for the window --
  the code span's ground, the strikethrough, the link rule, the rect a click
  gets back -- and it is one loop because it was two, one per surface that
  paints a `doc::BlockLayout`, and they had drifted too. The rect it hands back
  is `ui::LinkRegion`, which is why that type is in `ui/` and not `app/`.
- `src/export/` -- turning a note into a file that is not a note. Today that
  is PDF, and the shape is worth knowing before adding to it: the *format* is
  app-agnostic and lives in `src/core/pdf/` (objects, streams, the xref,
  `FlateDecode`, sfnt parsing, font embedding), and what lives here is the
  composition -- the page, the pagination, and painting a `doc::` layout onto
  it. `export/PdfBlocks.cpp` is the counterpart of `app/PageViewPaint.cpp` and
  the two are deliberately **two painters over one layout, not two renderers**:
  the block partition, the line breaking and the run styling are `doc::`'s and
  are computed once for whichever surface asks. Anything that has to *decide*
  something about a note belongs down in `doc::` -- `export/PdfComplex.cpp` was
  the exception and stopped being one: it carried an md4c renderer of its own,
  which is how it came to draw only the first table in a block, to set every
  cell as plain text, and to show a footnote definition as grey monospace
  source when the screen showed a footnote. It shapes through
  `doc/RenderLayout.h` now.
  Inside this layer there is one run painter, `exporting::paintRuns`, and both
  page painters go through it. The per-block ink overrides that look like they
  need a callback -- a quote's muted body, a ticked task, a callout's head line
  -- all reduce to *the ink a `TextRole::Body` run takes*, which is one colour.
  The screen has the same treatment, in `ui/DocRuns.h`, and
  `architecture_a_run_is_inked_once_per_surface` holds it: there are two output
  surfaces, so there are two of those loops and no more. This layer sits above
  `ui` because it reads the theme's palette and resolves the vendored faces
  through `ui::faceFile`, and it must never reach `app` -- an export takes
  notes and returns bytes, which is what lets it be tested with no window at
  all.
  Three things about the font side are worth knowing before touching it, and
  all three are in `src/core/pdf/`. `PdfFont` is the only thing that knows text
  is made of characters, and it *measures* and *encodes* -- both, from one
  `SfntFont`, because a measurement from one face and an encoding from another
  is text that overruns its own line breaks. `SfntKern.h` reads the pair
  kerning out of `GPOS` (no vendored face has a `kern` table at all, so a
  reader of that one would have kerned nothing while looking like it worked)
  and `PdfFont` resolves it into the `TJ` arrays of the content stream rather
  than leaving it to a shaper, because there is no shaper on the other side of
  a PDF. And `SfntSubset.h` cuts the embedded face down to the glyphs the pages
  showed -- which is a filter rather than the font toolchain it was assumed to
  be, and only because the encoding is `Identity-H` with an `/Identity`
  CID-to-GID map: glyph numbers never move, so nothing has to be renumbered.
  Do not renumber them.
- `src/app/` -- the surfaces themselves, the routers and the loop.

That list is not a description, it is where things go. `src/ui/` had become a
second grab-bag -- a trim, a lowercase, a fuzzy matcher, colour arithmetic, a
URL decoder, a MIME table, a tag parser -- and the tell was that twenty of its
twenty-four files compiled into `micronotes_core` and four into
`micronotes_shell`. A directory whose contents land in two targets is two
layers wearing one name.

**`src/core/` used to be a byte-identical vendored copy shared with a sibling
repo, kept in step by `tools/sync-core.sh` and a `CORE.sha256` manifest that a
ctest checked.** That is gone. The sibling never adopted the layout, so the
manifest was policing a copy that did not exist while adding a four-step dance
to every core edit. Do not reintroduce it: if a second app ever wants this code,
make it a library with a version, not a hashed copy.

Three targets are built from it. `micronotes_core` is everything above;
`micronotes_shell` is every surface that draws (`src/app/`, plus the
`src/ui/` files they draw through, plus `src/export/`, which needs the same
faces and the same image decoder); `micronotes` is `main.cpp` and a link line.
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

**Do not take a timing measurement while one is running.** A sanitizer build
saturates every core, and a `--screenshot` run or a harness pass taken beside
one reads two to three times its real cost -- consistently enough to look like a
result. The counters are unaffected, because they are deterministic; the clock
is not. Check `uptime` before believing a number.

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
through the same `app::documentMetrics` the note page lays out with -- and a
persistence lane that goes through `AppState`'s real writes to disk, but no
window, no textures and no present, and nothing in it goes through `src/app/`'s
key handling or its shell surfaces. So for anything above that a real session
*is* the instrument, not a nicety. The fifth pass in `docs/performance.md` found
1.1 ms of `fsync` on every keystroke and a right-hand panel costing more per
frame than the note; the seventh found a note's pictures being re-resolved on
disk once per layout. No harness lane could see any of them.

**A lane the harness does not have is a budget nothing enforces.** The eighth
pass is the cautionary one: every budget in the file was a layout or a paint, so
autosave -- which runs once a second while somebody is typing -- had grown a
whole-library tree walk and three whole-file reads, at 18.7 ms and 39,350
allocations a save, with the suite green throughout. Writing the lane *first*,
before touching the code, is what made that pass arithmetic rather than opinion.
The persistence lane is wall-clock, deliberately: a durable write is two `fsync`
barriers, and on process CPU time an 18 ms save reads as 60 us of work.

The harness is `tools/PerfMain.cpp` -- the fixture library and the list of lanes
-- over `tools/perf/`, which is one file per lane plus `perf/Harness.h`, the
clocks and the allocation counter every lane measures through.

The **shell lane** (`tools/perf/ShellLane.cpp`) is the one to add to when the
work is above `doc::Layout`. It
drives a real `UiRuntime` through a keystroke -- the editor, the reading page,
the outline panel, the status bar, the raw pane -- over the real faces and stops
short of the paint. Anything memoised on `ui.editor.revision()` is by
construction recomputed on every keystroke, and the memo makes it look handled;
that is the shape this lane exists to catch, and it caught one the day it was
written (see `docs/performance.md`, "The shell lane").

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
    --size 1600x1000 --pane split --panels sidebar,right --screenshot /tmp/shot.png
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
- **Never write a note's file without checking what is there.**
  `ui::OpenNoteRecord` carries a `platform::FileSignature` per open note and
  `AppState::writeOpenNote` -- the one path every save, rename, icon and tag
  edit goes through -- compares it before writing; a path that skips that
  comparison can destroy an edit made in another program. If two versions of a note exist and cannot be
  merged, both end up in the library -- micronotes does not choose. See
  `docs/library-format.md`, "Changes Made Outside micronotes".
- Prefer RAII, explicit ownership, and value semantics. Reach for inheritance
  only at a durable polymorphic boundary.
- The note area has three arrangements, and they are `microcore::ui::PaneMode`:
  raw Markdown source (`Ctrl+1`), a read-only rendering (`Ctrl+2`), and the two
  side by side (`Ctrl+3`), which is the default. There is one renderer for the
  rendering -- `app/PageView.h`, driven by `app/ReadingPage.h` -- and the source
  side is `app/RawPane.h`. **There used to be a fourth, "live", which rendered
  the formatting in place around a caret you could type into; it is gone.** Do
  not reintroduce a second renderer for a note: that is what `PageView` was
  merged out of, and the six ways the two had drifted are in this file's history.
- **Where a unit goes depends on how often it runs.** Release carries no LTO --
  which was measured, not assumed, and LTO itself was measured and is a mixed
  result rather than a win (`docs/performance.md`, "The tenth pass"). So a unit
  that runs per token, per block or per frame is defined in a *header*
  (`doc/Flow.h`, `doc/Tokenize.h`, `DocumentLayout`'s accessors), because a
  two-line body in another translation unit is an opaque call on the hottest
  path in the app; moving `Flow` out of `Layout.cpp` took `layoutBlock` from
  2,073 emitted instructions to 919. A unit that runs per update or per
  keystroke gets its own translation unit (`doc/LayoutUpdate.cpp`). Testability
  is the reason to separate a unit at all, and a header gives that just as well.
- **Before writing a test fixture, look for it too.** `tests/TempDir.h` is an
  RAII directory under the system temp root -- every disk test used to open with
  the same two lines and half of them ended with a third that a failed assertion
  never reached, so the suite leaked a tree per failure. `tests/ShellFixture.h`
  is a shell with a library and one note open, `tests/LayoutFixture.h` the stub
  face and the fixture note every `doc::Layout` test measures against, and
  `tests/EditorFixture.h` the word count spelled deliberately unlike the
  editor's own.
- Before writing a helper, look for it. `core/util/StringUtil.h` has `trim`,
  the ASCII case fold, `isAsciiSpace`, `splitLines` and `ellipsize`;
  `core/util/TextSearch.h` is the one literal search over a buffer -- every
  match, the whole-word predicate and the three "which match comes next" walks
  -- and it exists because the find bar, the note page and the raw pane each had
  a scan of their own and so gave three answers to one question;
  `core/editor/SoftWrap.h` is the one wrap of a buffer to a column that keeps
  each row's source range -- there were two of those and the second had no
  callers left, which is how a tree comes to break lines by two different
  rules; `core/util/Utf8.h` has the
  boundary walks; `core/util/Hash.h` is the one FNV; `core/platform/PathUtils.h`
  has `uniquePath`, `sanitizeFileStem` and `displayPath`; `ui/Memo.h` is how a
  memoised value is spelled; `ui/TextFit.h` is the measured text helpers;
  `library/Metadata.h` has the two front-matter shape rules
  (`continuesFrontMatterValue`, `frontMatterSequenceItem`), which anything
  reading `NoteMetadata::extra` back has to split those lines on. Every
  one of those exists because the same thing had been written two or three
  times, and in most cases the copies had drifted -- three different `trim`s,
  two spellings of FNV, four `nextBoundary`s, three `uniquePath`s.
- **A list of rows is two halves of one type, and both already exist.**
  `ui::RowCursor` (`ui/RowCursor.h`) *places* them: it advances by exactly the
  height it just used, so the list tiles, and it answers "does this one clear
  the foot" and "what is the mean row" for a pane that stops. `ui::rowBand`
  (`ui/RowBand.h`) *queries* them, and is only correct because the cursor tiled
  them -- which is why they are a pair. How far a list is scrolled is
  `ui::ScrollList` in pixels or `ui::RowStrip` in whole rows (`ui/ScrollList.h`);
  a `RowStrip` carries the row count the paint recorded alongside the offset,
  because for a variable-height list nothing else can know it. Four surfaces had
  written the placement out for themselves, and the copies had grown separate
  fields for the same two concepts.
- A context menu is opened by `ui::anchoredMenu` and `ui::openMenu`, which are
  the eight lines every one of them wrapped its rows in. `openMenu` is where
  the row cap is set from the rows there are, because a context menu shows all
  of itself -- the `Overlay` default is a *palette's*, twelve with the rest
  scrolled, and a menu that scrolls hides the destructive row this tree puts
  last. Five of the six set it by hand and the sixth did not.
- A popup menu's row is `ui::MenuRow`, painted by `ui::drawMenuRow`, and a
  popup's rows are stacked with `ui::menuPopupRows`. The menu bar's popups, the
  context menus and the palette are one object reached through two item tables;
  when they were two paints and two stackings, the tick sat a pixel apart and
  the accelerator a whole size apart between them.
  **A row of either table is built by name, never positionally.** The bar's is
  `ui::MenuItemSpec` through `item()`/`sep()` in `ui/Menus.cpp`; a context
  menu's is `ui::OverlayItem` through `ui::menuItem` and `ui::menuSeparator`,
  with `.enabledIf`, `.ticked`, `.destroys`, `.withKeys` and `.withDetail` for
  the rest. `OverlayItem` has eight fields and four of them are bools, so
  written out in order a row's meaning was which trailing `false` had become a
  `true`; `architecture_a_menu_row_names_the_field_it_sets` fails on the
  separator's old spelling. A designated initializer would say the same thing
  and GCC 13 warns on every field it leaves defaulted, which is the point of
  using one.
  **And a context menu's item id is the action's name.** The note menu and the
  notebook menu carry no routing of their own beyond the one row that is not
  the action of that name -- `handleOverlayResult` hands the id to
  `performCommand` -- so a row a right click offers and the bar's row for the
  same command cannot come to mean different things. The `files` tree's two
  menus are about `ui.sidebar.companionTarget` rather than the selection, so
  their ids say `-file`: calling them `rename` and `delete` left them one
  fallback away from acting on the note instead.
- What a `PageView` needs before it lays out is `app::PageFrame`, handed over in
  one `beginFrame`. Do not add a per-frame setter: the reason that struct exists
  is that a page which is not told about a new revision does not fail, it keeps
  a stale layout, and the two surfaces assembling the inputs by hand is how the
  reading pane came to render `[[wikilinks]]` as literal brackets.
- A press or a keystroke is routed by one chain whose *order is the design*, and
  each band or surface owns what it means: `app/PointerRouter.h` into
  `app/PagePress.h` / `Sidebar::pressSidebar` / the panels, and
  `app/KeyRouter.h` into `app/KeySurfaces.h`. Put new behaviour in the surface,
  not in the router. `../microide` is the reference for both shapes.
- **Read `../microide`'s `dev-docs/` before debugging anything the window system
  owns.** `dev-docs/platform/wayland-stale-cursor.md` is a cursor bug that
  survived years there, and two of its three findings applied here unchanged:
  `SDL_SetCursor`'s result has to be checked before recording the shape as
  applied, or one failure convinces the shell that shape is already showing
  forever; and SDL's Wayland hit-test path can change the *displayed* cursor
  without updating the handle SDL holds, so a re-assert has to pass through a
  different cursor first. The third -- a dropped present leaving the new shape
  queued -- does not apply only because this loop repaints on every event. If
  that is ever narrowed to "repaint when something visible changed", the cursor
  needs a present of its own: hovering a link changes no pixels at all. See
  `app/Cursor.h`, and `app/CursorTheme.h` for why SDL may not have a hand to
  show in the first place.
- Avoid hidden coupling through mutable global state. The perf tables are the
  deliberate exception, and they are process-wide by design.
- Everything that changes between two events -- a queued window action, a
  change another program made to the library, the caret's blink, a finished
  file chooser, the autosave clock -- is `app/Pending.h`, in one list in the
  order it has to run. Each of them arrived as one more `if` in the event
  loop, which is what the line budget below exists to push back on.
- `src/app/Application.cpp` is the window, the renderer and the wait, and
  nothing else. It was a 2,700-line catch-all; the dispatch over event *kinds*
  is `app/EventRouter.h` and the routing of a press or a keystroke once it is
  known to be one is `app/PointerRouter.h` and `app/KeyRouter.h`; the frame is
  `app/Frame.h`, when to sleep and what to be awake for is `app/FramePolicy.h`,
  when the buffer is written back is `app/Autosave.h`, the command chain is
  `app/Commands.h`, and what the command line asked for is `app/Startup.h`. `ArchitectureTests` holds that file to a line budget that only
  ever goes down, **and every other `src/app/` source to a 1,000-line ceiling** --
  because a budget on one file by name does not stop a catch-all, it only stops
  that one. New behaviour wants a named unit, not another function in an old file.
- The shell's state is a composition, not a struct. `UiRuntime` in
  `app/Shell.h` holds what is genuinely shell-wide -- the library, the buffer,
  the focus, the status line -- and each surface's state is a named type in its
  own header (`app/SidebarState.h`, `app/PanelState.h`, `app/ChromeState.h`,
  `app/EditingState.h`, `app/PointerState.h`, `app/RawPaneState.h`,
  `app/TextFields.h`, `app/FindState.h`, `app/StatusBar.h`'s `StatusBarState`).
  It is that composition and nothing else: the cursor's
  types are `app/Cursor.h`, the caret's frame policy `app/CaretPolicy.h`, the
  layout pass `app/Layout.h`, and a drawn link's rect `app/LinkRegion.h` --
  which is what lets a drawing helper stop including the whole runtime to name
  one parameter. Two things follow. A function should take the part it
  works on rather than the whole shell. And a rule about one surface's state
  belongs as a *method* on that surface's type, not as arithmetic repeated at
  each call site -- that is where this codebase's quieter bugs have come from.
- **A floating surface's geometry is one function, shared by the paint and the
  hit test.** `app/FindBar.h`'s `findBarLayout` and `app/StatusBar.h`'s
  placement are both written that way, and `../microide` says why in the same
  words: two copies of a button's rect is a button that moves out from under the
  pointer. Nothing about either is recorded as it is drawn, so a press works on
  a frame the surface has not been painted on yet -- which is the opposite rule
  from `app/ChromeState.h`, and that header explains the one constraint that
  forces the exception.
- **The status bar reports state; it does not echo actions.** Its segments are
  values (`app::statusSegments`), pure over the shell so the test reads the
  strings without a window, and each carries its own tone and the command a
  click runs rather than having either derived from its label at paint time. A
  message set through `ui.status` is *transient* -- see `app/StatusLine.h`,
  which is a struct with an assignment operator precisely so that the hundred
  and forty existing `ui.status = "..."` sites keep working and none of them can
  forget to stamp the clock.
- **A readout derived from the note is bounded by the edit, not by the note.**
  Every surface that derives something from the buffer memoises it on
  `editor.revision()`, and every one of those memos misses on every keystroke
  by construction -- so on a miss the question is not "recompute it" but "how
  much of it". `editor::TextEdit` is what makes that answerable: the splice,
  stamped with the revision it came from and the one it produced, so a consumer
  can *check* that the edit leads from the state it holds rather than trust it.
  Three things are already bounded this way and they are worth reading as one
  pattern -- `doc::DocumentLayout` relays only the blocks the edit moved,
  `editor::softWrapUpdate` rewraps only the logical lines it touched, and the
  status bar walks the caret's line and column from the place it last answered
  with rather than from the note's first byte. All three put the validity check
  on the *caller*, because only the caller knows what the standing value was
  built from; `ui::Memo::key()` is what they read to find out. And all three
  are proved the same way, by equality with the unbounded computation over a
  long sequence of edits rather than by a list of cases. `docs/performance.md`,
  the twelfth and thirteenth passes.
- A memoised value is `ui::Memo<Value, Key>`, not a value plus a `valid` flag
  plus the key spelled out field by field. The failure that shape has is
  specific: the comparison and the assignment are two lists, they drift, and
  the memo then answers for inputs it was not built from -- silently, because it
  is only wrong when the field nobody stored is the field that changed.
- A key binding lives once, in `ui::actionSpecs()`. `ActionSpec::keyRunsIt` says
  whether the key alone runs it; if it does, `handleKey` hands the name to
  `performCommand` and there is no branch to write. Only a chord whose meaning
  depends on the focus gets a hand-written branch. The chain used to be a second
  copy of the table, and three bindings -- `F2`, `Ctrl+Q`, `Ctrl+O` -- were
  advertised in the palette and the shortcut list while doing nothing at all.
- Debt goes in `docs/tech-debt.md` as a numbered `TD-n`, with what it costs
  today and why it has not been paid. There are no `TODO` comments in this tree
  and it should stay that way -- a TODO is invisible to everyone who is not
  already reading that file.

## Commits

One logical change per commit. Write the message so it explains *why* the change
was needed and what was wrong before -- the diff already shows what changed.
State measured numbers when the change is a performance one.
