# Tech debt

Known debt, each with a stable id so a comment in the code can point at it and a
commit message can close it. One entry per thing, and every entry says what it
costs today and why it has not been paid — an entry with no cost and no reason
is a preference, not debt, and does not belong here.

Performance debt that is part of a measured narrative lives in
`docs/performance.md` under its `### Open:` headings; those are cross-referenced
below rather than duplicated, because that file carries the numbers and the
history that make them make sense.

**Adding an entry:** take the next free number, never reuse one. Closing an
entry means deleting it and saying so in the commit; a register of things that
turned out to be fine is a register nobody reads.

---

## TD-6 — performance debt tracked in `docs/performance.md`

Not repeated here. Two things are left, and both are *decisions with numbers*
rather than work waiting to be done — which is why they are listed here rather
than left as open sections somebody has to re-measure to act on:

- **an edit still touches every block below it** (materialised positions vs. a
  Fenwick tree). The thing to reach for *if the shift ever shows up*: today
  `layout.blocks_shifted` is 1,812 against `layout.blocks`' 9,612 on average,
  and the query side — which a tree makes O(log n) — is the one on the render
  path.
- **the staging tokens are built only to be thrown away**. Measured at a 5%
  ceiling, paid for with `out.runs.reserve` ceasing to be exact. The section in
  `docs/performance.md` carries the breakdown.

## TD-14 — the raw pane still lays its own text out

`src/app/RawPane.cpp`.

The live page and the reading pane are one renderer now (`doc::Layout`'s token
flow through `PageView`). The raw pane is the third engine, and it is still
there: its own soft wrap over `editor::WrappedLines`, and its own line
stepping. The scrollbar arithmetic is no longer its own -- it holds a
`ui::ScrollList` like every other scrolling surface, which also took the whole-
note rewrap off the wheel, the cursor-shape query and the scrollbar drag: all
three asked `editorMaxScroll`, and answering cost a full soft wrap.

**What it costs today.** Every typographic decision that reaches all three panes
is two decisions rather than one. That is down from three, and the one that is
left is the one furthest from the others: the raw pane deliberately shows the
file as bytes, so its line breaks are the file's and not the layout's.

It used to cost one more thing, and that half is paid: `rawPaneRows` was keyed on
a **copy of the note**, so every keystroke copied a 200 KB buffer into the cache
key (17.6 us) and every frame compared the buffer against it (9.8 us) to answer
a question the editor's revision answers in one word. The key is
`(revision, column, text size)` now -- and the text size is new, because the
wrap width is a rect and does not move when the reader makes the text bigger, so
at one width the pane kept wrapping the note to a font it was no longer drawn in.

What is left is the rewrap itself, which is still the whole note on every
keystroke:

| | per |
|---|---:|
| full soft wrap | **807 us** per keystroke |

That is three times what the ninth pass removed from the outline panel and the
status bar put together, on the same event. What it needs is an incremental soft
wrap, which is the second engine this entry is about. If the pane goes, the
number goes with it.

**Why it is still here.** `RawPane`'s own header says it is "kept apart so that
replacing it is a matter of deleting one file", which is the right plan -- and
is now true of its *state* as well: the five fields it kept on `UiRuntime` are
`app/RawPaneState.h`, so deleting the pane deletes a header rather than picking
five fields out of a struct.

What it is waiting for is a decision rather than a refactor: whether a pane that shows
the source *as a monospaced file* is a thing this app wants at all now that the
live surface reveals a block's markers under the caret and drops a `Complex`
block to raw source when you click into it. If it is not, the file deletes; if
it is, it is meant to be a different engine, and the debt is only the geometry
it duplicates -- which `ui::pageRectIn` and `ui::pageColumnIn` already hold.

## TD-17 — the index stores a second copy of every note body

`src/library/LibraryIndex.cpp`, schema 4: `notes.body` and `notes_fts.body` both
hold the whole text of every note.

**What it costs today.** The SQLite index is roughly twice the size of the
library it indexes, on disk and in the page cache. For a 1,000-note fixture of
200 KB notes that is a few hundred megabytes of duplication; for an ordinary
library it is a few megabytes and nobody would notice. Every save writes the
body twice for the same reason -- once to the row, once to the fts entry --
though neither write copies it any more.

**Why it has not been paid.** fts5 supports an external-content table
(`content='notes'`), which stores no copy and reads the column from `notes` when
it needs it. That is the right answer and it is a schema bump plus a careful
look at three things: `collectRows` currently selects `notes.body` through a
join, which becomes free rather than cheaper; the `rowid` contract between the
two tables becomes load-bearing rather than an optimisation (see the comment on
`NoteWriter`); and an external-content table will not rebuild itself, so a
crash between the row write and the fts write leaves them disagreeing where
today it leaves them both stale. None of that is hard, and none of it is
justified by a few megabytes -- but the duplication should be a decision on the
record rather than an accident of the first schema.


## TD-19 — the harness has no lane for a keystroke through the shell

`tools/PerfMain.cpp`. Every edit lane drives `doc::Layout` directly.

**What it costs today.** It cost two findings in the ninth pass, each larger
than the layout work the existing budgets do measure: the outline panel rebuilt
its block partition on every keystroke (240 us on a 200 KB note) and the status
bar recounted the whole note on every keystroke (247 us), against a keystroke
whose layout update is 14 us. Both shipped, both were invisible, and both were
found by reading code rather than by any instrument. `TD-14` names a third of
the same kind, still open at 807 us.

The shape is specific and it will recur: anything memoised on
`ui.editor.revision()` is *by construction* recomputed on every keystroke, and
the memo makes it look handled. They are `ui::Memo` now rather than four loose
fields each, which makes them findable -- `rg 'ui::Memo'` lists them -- but
findable is not measured, and a memo keyed on the revision is exactly the thing
this lane would catch.

**Why it has not been paid.** The lane needs a `UiRuntime` and a `TextRenderer`
driven through a real key handler, and the pieces are all there now -- the shell
is a library, the test binary already builds a `UiRuntime`, and
`ui.editor.insert()` plus the surfaces' own entry points is most of a keystroke.
What is missing is a decision about what it measures: `drawApp` needs a renderer,
so either the lane stops short of the paint (and measures the models, which is
where all three findings were) or the harness grows a headless window and stops
being the thing that runs in three seconds with no display. The first is
worth doing and is an afternoon; it was not done in the same pass that found the
bugs, because a lane written to catch the bug you already know about is the one
that catches nothing else.

## TD-22 — the menu bar has no keyboard mnemonics

`src/app/MenuBar.cpp`, `src/ui/Menus.h`.

An open menu is fully navigable from the keyboard -- the arrows walk it, Left
and Right step to the neighbouring menu, Enter chooses and Escape shuts it --
but there is no way to *open* one without the pointer. `Alt+F` does not reach
File, and neither does F10.

**What it costs today.** Not much on its own: every item in every menu is also
an `ActionId` with a chord or a palette row, so nothing is unreachable. What it
costs is the claim the menu bar is there to make. The bar exists because a shell
whose only routes to a command are a chord and a palette is one where every
command has to be learnt before it can be used -- and a bar you can only reach
by taking a hand off the keyboard is half of that argument given back.

**Why it has not been paid.** Mnemonics are not one change but three. The label
needs to carry which letter is the mnemonic (microide's `MenuSpec` does not
model this either, so there is nothing to copy); the draw needs to underline
that letter, which means measuring a prefix and a substring rather than a label;
and `Alt` has to stop being an ordinary modifier in the key chain -- micronotes
binds `Ctrl+Alt+Left`/`Right` to the panel toggles, so a bare `Alt` press has to
be distinguished from `Alt` held as part of a chord, which is a keyup-driven
state machine rather than a branch. F10 alone would be a third of a fix and
would sit oddly next to a bar that does not underline anything.

## TD-23 — `DocumentLayout::update` is one algorithm in one function

`src/doc/Layout.cpp`. `update` is 353 lines and `layoutBlock` is 310.

Down from 409 and 1,867 respectively: the three pieces that were self-contained
have been taken (`geometryKey`, `mergeDirtyRanges`, `sweepLayoutCache`), the
read-only half of the class is `LayoutQueries.cpp`, and the file is 1,541 lines
rather than 1,867. What is left in `update` is the incremental algorithm itself.

**What it costs today.** The five phases inside it -- decide whether the
standing partition still describes the source, absorb the edit, align the
placement to the new indexing, walk the dirty ranges, carry the outstanding
shift down -- read and write locals the next one depends on. `head`, `tail`,
`patchable`, `geometry`, `caretBlock`, `rawBlock`, `count`, `previousCount`,
`shift`, `pendingTop`, `pendingRows`, `settled`. Extracting a phase means
passing eight or nine of those, which is the shape that says a function is one
algorithm rather than several.

**Why it has not been paid.** Because the honest fix is a carrier -- an
`UpdatePass` holding the per-call state, with the phases as its methods -- and
that is a bigger change than it looks: the state is exactly what makes the
phases *phases*, so the carrier has to get the ownership right or the split is
worse than the function.

This entry was once grouped with two others as "the carrier problem in four
places, worth doing once with one shape". Those two were a column of bands with
a running `y`, and they are paid: the cursor that produces them is
`ui::RowCursor`. This is not the same problem and never was — an incremental
algorithm whose locals *are* its phases — and one shape covering both would
have fitted neither.

The verification is no longer the problem, and that is worth recording because
it was the reason given last time. The layout's counters are deterministic and
they cover the reuse paths densely: `blocks_relaid`, `blocks_walked`,
`blocks_shifted`, `blocks_key_reused`, `cache_hits`, `cache_sweeps`,
`cache_evictions`, `placement_patches` against `placement_rebuilds`,
`fold_resolutions_skipped`. A `tools/session-compare.sh` run across the three
panes reports every one of them byte-identical or it does not, and that is
exactly what caught nothing and confirmed everything in the three splits above.
So the instrument is there; what is missing is the design decision about the
carrier.

`layoutBlock` is a separate entry waiting to be written: it is 310 lines of
typesetting -- one arm per block kind -- and unlike `update` it has no shared
mutable state, so it splits by kind whenever anybody wants to.

## TD-34 — nothing scrolls sideways

`app/Scroll.cpp`'s `routeWheel` takes `wheel.y` and the loop hands it nothing
else: `event.wheel.x` is read nowhere in the tree. No surface carries an
x-offset either -- `ui::ScrollList` and `ui::RowStrip` are one axis, and
`PageView` has `scroll()`/`maxScroll()` and no horizontal pair.

**What it costs today.** Content wider than its column cannot be reached. A
table wider than the page, a code line longer than the column and a note title
longer than the sidebar are all clipped and that is the end of it -- the paint
already installs the clip (`PageViewPaint`'s `columnClip` keeps a code block
"inside the column: a long line scrolls off its own right edge rather than out
over the gutter"), which is the right treatment only if there is a way to
follow it. There is not. The reading pane's `00-Index`-style tables are the
common case: a fifteen-row table of links whose last column is unreachable.

A trackpad's horizontal gesture is also silently dropped rather than falling
back to a vertical scroll, so a two-finger swipe that is slightly off-axis
scrolls and one that is on-axis does nothing.

**Why it has not been paid.** It is not one change. Every scrolling surface
grows a second axis, `ui::scrollbarGeometry` becomes two bars with a corner
between them, `ui::RowCursor`/`ui::rowBand` are vertical by construction, and
the wheel router has to decide what a diagonal gesture means. The prior
question is a design one and is worth answering first: a Markdown reader may be
better served by *wrapping* what is too wide -- a table that reflows, a code
block that soft-wraps with a continuation marker -- in which case the axis is
never needed and the clip becomes the bug rather than the containment. Doing
both would be the worst outcome, so the decision comes before the work.

## TD-35 — a resize repaints a frame behind the window

`src/app/Application.cpp`'s loop, and `WindowChrome`'s borderless window.

Every frame is drawn from scratch and presented with VSync on. The window is
`SDL_WINDOW_BORDERLESS` with a custom hit test, so a drag on its edge is an
*opaque* resize the window manager performs: the X window grows, and the region
it grew into holds undefined content until micronotes presents. The loop drains
the resize events, draws once, and presents -- so there is one frame of
unpainted window per resize step, which reads as a flicker to black for the
whole drag.

**What it costs today.** Measured over ten scripted `xdotool windowsize` steps
on a 1000x700 window with a 400-note library: 18 presents, `frame.draw_micros`
42,182 and `frame.present_micros` 194,944 -- so 2.3 ms of drawing per frame
against 10.8 ms of presenting. The draw is not the problem and neither is a
missed frame: the app repaints on every resize step. What is missing is anything
on screen in between.

**Why it has not been paid.** The fix is a retained scene texture: keep the last
frame, blit it immediately when the window is exposed or resized, and draw the
real frame after -- so the window is never unpainted, only briefly stale. That
is what `../microide` does, and it is four files there
(`SceneTexturePresenter`, `ApplicationPresentationCache`, `DirtyRegionPolicy`,
`RedrawTraceAccumulator`) because the retained texture also wants a
dirty-region policy to be worth its memory. micronotes presents the whole
window every frame, so it would get the flicker fix without the partial-redraw
half -- but it is still a new stage in the frame, with its own lifetime against
the renderer, and it belongs in a pass that measures it rather than in one that
notices it.

