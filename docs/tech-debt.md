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

It used to cost one more thing, and that half is paid: `editorRows` was keyed on
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

## TD-16 — a header edit writes the body it read off the disk

`AppState::saveSelectedNoteHeader`, and the three callers that go through it:
`renameSelectedNote`, `setSelectedNoteIcon`, `updateSelectedTags`.

All three change only the note's front matter, and all three get the *body* to
write back by reading the file. So each of them depends on an invariant nothing
states or checks: that the editor buffer has already been saved, and the disk
therefore holds the same bytes the buffer does.

**What it costs today.** Nothing visible, because the invariant does hold. Every
path into these three saves first -- `beginRename` and `beginTagEdit` call
`saveCurrent`, and an open overlay captures input so the buffer cannot become
dirty between the prompt opening and being committed. It also costs a whole-file
read and a whole-file write per icon or tag change, of bytes the editor is
already holding.

**Why it has not been paid.** The fix is to pass the body in -- these become
`saveSelectedNote` with a different header -- which means the three callers hand
over `ui.editor.text()`, and `AppState` stops being able to write a note's
header without the shell's cooperation. That is probably the right shape, and it
is a change to three signatures and their call sites for a bug that cannot
currently happen. It is here because "cannot currently happen" rests on the
overlay's input capture, which is a UI decision a long way from this file: the
day an overlay stops capturing, or a shortcut sets an icon without one, this
silently writes a stale body over a fresh one and there is no test that fails.

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

## TD-18 — the watcher's degraded mode has no test

`platform::DirectoryWatcher`, `kMaxWatches` and every path that calls
`requestRescan()` because it could not name what changed.

**What it costs today.** Nothing measurable, and it is the code most likely to
matter on somebody else's machine. A library larger than the per-user
`max_user_watches`, or larger than the watcher's own 8,192-directory budget,
falls back to asking for a full refresh instead of naming paths -- which is
correct, and is exercised by no test. Neither is the kernel's `IN_Q_OVERFLOW`
path.

**Why it has not been paid.** Both need a fixture that is expensive or
privileged to build: eight thousand directories, or enough events in flight to
overflow a 16,384-event queue. The rescan *request* is covered -- the
tree-changes-shape test asserts it -- so what is untested is the two triggers
rather than the response to them. `SetEntryBudget`-style injection (a testing
seam that lowers the budget) is the obvious way in, and is worth adding the next
time this file is opened.

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

## TD-21 — a capture waits half a second no matter what it is capturing

`src/app/Screenshot.cpp`.

`captureWindowToFile` draws sixty frames with an `SDL_Delay(8)` between them
before reading pixels back, on a comment about giving the compositor time to map
and size the window. That is a fixed ~500 ms floor on every `--screenshot` run.

**What it costs today.** `tools/session-compare.sh` is built on interleaved
captures — the instrument that the harness's own lanes cannot replace, per
`docs/performance.md` — and every one of them pays the floor whether the window
mapped in one frame or thirty. It also makes the capture path the slowest thing
in the test tooling by an order of magnitude, which is the sort of cost that
quietly discourages taking the before-and-after pixels the guide asks for.

**Why it has not been paid.** It needs a real readiness signal rather than a
sleep: `SDL_EVENT_WINDOW_EXPOSED` plus a size that matches what was asked for
would let the loop stop as soon as the window is actually up, falling back to
the sixty-frame bound only when neither arrives. The window is now mapped
explicitly by `app::revealWindow` rather than at creation, so the moment to
watch for is finally a moment the code chooses — before that the map raced
everything and the sleep was standing in for a signal nobody had. Not done here
because the capture path is used by tooling rather than by the app, and this
change was about the app's first frame.

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

## TD-24 — two perf counters are not deterministic

`layout.caret_queries` and `layout.caret_probes`.

AGENTS.md says of the counters: "**Deterministic**: the same workload gives
byte-identical values every run and in every build type, which is what makes
them proof rather than evidence." That is true of every counter but these two.

**What it costs today.** They appear in the diff of about half of all
`session-compare.sh` runs, in both directions -- 114 against 99 one run, 96
against 114 the next -- on changes that cannot possibly have touched them. Which
is worse than useless: it trains whoever is reading the diff to skim past
counter changes, on the instrument whose whole value is that a changed number
means something happened.

The cause is that `PageView` asks `caretRect` only when the caret is *painted*,
and whether it is painted is `ui::CaretBlink`'s answer to `SDL_GetTicks()`. A
capture draws sixty frames as fast as it can, so how many of them land in the
caret's on-phase is a function of how fast the machine was that second.

**Why it has not been paid.** The fix is to make the capture's clock
deterministic rather than to move the counter, because the counter is measuring
the right thing -- and a blink that does not advance during a capture is also
what would make a *screenshot* of a caret reproducible, which is the same
problem one step further on. That is a seam through `CaretBlink` and the
capture path, and it is worth doing with `TD-21`'s readiness signal, which is
in the same file for the same reason.

## TD-27 — the two pages wire and feed themselves separately

`src/app/LivePage.cpp` and `src/app/ReadingPage.cpp`.

`wireLivePage` and `wireReadingPage` install the same three `PageViewHooks`
(`measureComplex`, `wikiLinkResolves`, `drawComplex`) with the same three
lambda bodies, in a different order, then both call `wirePageImages`.

And `drawLive` and `drawReading` call fifteen of the same `PageView` methods,
in the same order, opening with the same seven-call feeding sequence -- wiki
revision, image revision, source revision, edited span, pointer, header
height, layout -- followed by the same
`applyQueuedAnchorJump`, the same `sweepComplexCache`, and the same push of
`links()` into `ui.linkRegions`.

The nine calls that are the live page's alone are the ones that say what the
difference actually is: `setFolds`, `setFoldsActive`, `setBlockSelection`,
`setDropOffset`, `setSelecting`, `setCaretVisible`, `setRawOffset`, `rawOffset`
and `revealCaret`.

**What it costs today.** Every input a page needs is added twice, and the
failure mode is silent and asymmetric: a page that is not told about a new
revision does not break, it *keeps a stale layout*. The header already records
one instance of exactly that -- the reading pane rendered `[[Some Note]]` as
literal brackets for as long as it did because it had not been given the
wikilink pass the live surface had.

**Why it has not been paid.** The two are genuinely not the same page -- the
nine calls above are the proof, and any shared helper has to leave room for
them. So the shared part is the *frame contract*: "here is everything a
`PageView` needs to know before it lays out". Factoring it means naming that,
which is a design decision about `PageView`'s interface rather than a code
move -- and it overlaps `TD-30`, because the reason the feeding sequence is
seven calls long is that `PageView` has sixteen setters and no one call to
make.

## TD-29 — which field the focus names is answered by three switches

`focusedField` in `src/app/Fields.cpp`, `caretStateKey` in `src/app/Shell.h`,
and `handleFieldKey`'s Enter arm in `src/app/KeySurfaces.cpp`.

The first two are the same switch over the same five `FocusArea` values
returning the same five `TextFields` members, once mutable and once const. The
third maps three of the five to their commit verb.

**What it costs today.** A sixth field is three edits, and the one that is
forgotten is `caretStateKey` -- where the symptom is a caret that blinks through
a burst of typing in the new field and nothing else. That is the failure the
key was introduced to prevent, so the shape currently reintroduces it once per
field added.

**Why it has not been paid.** The mutable/const pair wants an overload, which is
two lines and could be done now. The Enter arm is the interesting half: it says
that `FocusArea` is carrying two things at once -- which surface has the
keyboard, and which of five prompts is open -- and the honest fix is to give
`TextFields` a way to name its own members so the three switches become one
table. That is a change to `FocusArea`, which has eight values and 88 mentions
across
22 files.

## TD-30 — `PageView` has sixteen setters and no one call to make

`src/app/PageView.h`, 51 methods.

The file split is done. `PageView.cpp` reached 1,003 lines,
`architecture_no_shell_source_is_a_catch_all` failed the build, and the paint
half went to `PageViewPaint.cpp` -- `draw` plus the eight `draw*` methods, the
only part that touches an `SDL_Renderer` -- with the vocabulary the two halves
share (`toTextStyle`, `colorFor`, `toRect`, the gutter offsets) in
`PageViewStyle.h`. Same class, private state untouched; the split is
`Layout.cpp` / `LayoutQueries.cpp` again.

**What is left, and what it costs today.** The sixteen setters. They exist
because a caller has to assemble the inputs for one layout by hand, which is
what makes `TD-27` two eight-call sequences, and the failure mode there is
silent: a page not told about a new revision does not break, it keeps a stale
layout.

It also holds raw `N.0f` pixel literals against a `ui::kSpace*` scale it names
its own constants beside.

**Why it has not been paid.** The interface half is `TD-27`'s: the shared thing
is the *frame contract*, "here is everything a `PageView` needs to know before
it lays out", and naming that is a design decision rather than a code move.
Doing the file split first deliberately took the ceiling pressure off without
addressing it, which is why this entry stays open rather than closing with the
split.

## TD-31 — `OverlayKind` is dispatched by `if` at seventeen sites

`src/ui/Overlay.cpp`. Five kinds -- `TextPrompt`, `List`, `Confirm`,
`GlyphPicker` and the tag grid -- asked about seventeen times: ten
`kind == OverlayKind::X` comparisons plus seven calls to the `isGridOverlay`
helper, spread across `layoutFor`, `handleKey`, `handleClick` and `draw`, each
of which asks more than once.

**What it costs today.** `OverlayStack::draw` is 210 lines and `layoutFor` is
129, and in both the per-kind arms are interleaved with the parts that are
common to every kind. A sixth kind means finding all seventeen, and the
compiler helps with none of them -- these are `if` chains over an enum, not
switches, so there is no `-Wswitch` to fall back on.

**Why it has not been paid.** The obvious fix -- a `struct OverlayBehaviour`
per kind, or virtual dispatch -- is more machinery than five kinds justify, and
`AGENTS.md` is explicit that inheritance is for a durable polymorphic boundary.
The cheaper and probably better fix is to make the *questions* explicit rather
than the kinds: `takesTypedText`, `hasRows`, `isGrid`, `hasConfirmButton` are
what the sites are actually asking, `isGridOverlay` is already one of them --
and it is
the seven-call half, which is the evidence that this is the direction that
works. Four such predicates would turn seventeen enum comparisons into four
named facts
about the overlay. That is a naming exercise, and worth doing next time this
file is opened for another reason.

## TD-32 — `rebuildSidebarRows` is a builder written as seven lambdas

`src/app/SidebarModel.cpp`, 236 lines, of which about 200 are seven lambdas --
`pushCaption`, `pushSection`, `pushNoteShortcuts`, `pushTree`,
`pushSearchResults`, `resolvable`, `finish` -- capturing the same running
cursor, and about 35 are the band sequence that calls them.

**What it costs today.** The same shape as `TD-23` and for the same reason: the
lambdas share mutable state (`y`, the row vector, the metrics), so the function
cannot be split by moving pieces out of it. It reads as a 236-line function and
is really a small builder with its state in the enclosing scope.

**Why it has not been paid.** A `RowCursor` type holding the vector, the running
`y` and the metrics, with the seven pushes as methods, is the answer, and it is
a better one than it looks: `sidebarRowRange` already depends on the rows
tiling -- every push advancing the cursor by exactly the row's own height -- and
that invariant is currently maintained by seven lambdas agreeing to. A type
would own it. Not done because it is one instance of the same carrier problem
in four places (`update`, this, `Overlay::draw` and `drawSettingsSurface` --
`TD-23` and `TD-35`), and they are worth doing together once, with one shape,
rather than four times with four.

## TD-33 — `AppState` is 55 methods, and the widest is `workspace()`

`src/ui/AppState.h`.

**What it costs today.** `WorkspaceModel& workspace()` hands out a mutable
reference to the whole view model, and it is reached through at **62 sites** as
`ui.state.workspace().something`. So `AppState`'s encapsulation is whatever
`WorkspaceModel` chooses to make public, and 21 of those 62 are
`ui.state.workspace().paneMode()` -- a question the shell asks constantly, three
objects deep, about the thing it is drawing.

The 55 methods themselves are mostly fine: `AppState` is an aggregate root and
most of them are one-line delegations to `library_`, `index_` or
`organization_`. What is not fine is that a reader cannot tell which is which,
and that the note-writing path -- `saveSelectedNote`, which stats a file before
overwriting it so that an edit made in another program is filed beside the note
rather than destroyed, and which `AGENTS.md` singles out as the rule never to
bypass -- sits in the same list as `toggleFavorite`.

**Why it has not been paid.** Splitting the class is the wrong first move: the
selection, the library, the index and the workspace really are one thing with
one revision counter, and pulling them apart would put the revision in two
places. The first move is narrower and worth doing on its own: the reach-through
is a Law-of-Demeter problem, not an ownership one, so the fix is that callers
which only *read* the pane mode should not be handed a mutable workspace. That
is either a `paneMode()` on `UiRuntime` or a `const WorkspaceModel&` overload
used by default, and it is 62 mechanical sites -- which is why it wants to be
its own commit rather than a rider on something else.


## TD-34 — one popup shape, laid out by two engines

`src/ui/Menus.cpp` (`menuPopupRect`, `menuPopupItemRect`, `menuPopupItemAt`,
over `MenuItemSpec`) and `src/ui/Overlay.cpp` (`OverlayStack::layout`, over
`OverlayItem`).

**What it costs today.** A menu-bar popup, a context menu and the command
palette are one object -- a card holding a list of commands, each with an
accelerator, a tick column, a disabled state and rules between the groups --
and two independent pieces of code decide where its rows go and which row a
click landed on. The two item structs have converged field by field:
`MenuItemSpec` has `label`, `separator` and `checkable`, `OverlayItem` has
`label`, `shortcut`, `separator`, `checked`, `enabled` and `destructive`, and
the accelerator is spelled `menuItemAccelerator(spec)` on one side and carried
in the struct on the other.

They already share what was cheapest to share: `ui::drawMenuRow` paints a row
for both, and `ui::menuRowHeight` and the `kMenuPopup*` constants are now read
by both. What is still written twice is the stacking -- walk the items, add each
one's height, and hand back a rect per index -- and the hit test over it, which
is the part where a discrepancy is a wrong command run rather than a wrong
pixel.

**Why it has not been paid.** The two are not the same function with two
callers; the overlay's layout also places a filter field, a swatch grid, two
confirm buttons and a hint, and it scrolls, while a menu-bar popup is capped to
the window and deliberately never scrolls. So the merge is not "delete one" but
"extract the row band both build" -- a `rowBand(items, top, width)` over a span
of something both item types can present as, plus the hit test on it. That is a
real interface decision -- which type the band walks, and whether
`MenuItemSpec` becomes a projection into `OverlayItem` rather than a second item
struct -- and it is worth making deliberately, in a commit of its own, with the
hit test moved under `MenusTests` and `OverlayTests` together. Taken as a rider
on a feature, the two would end up with a shared helper each.

## TD-35 — `drawSettingsSurface` is the fourth carrier-in-a-function

`src/app/SettingsPane.cpp`, 772 lines, of which `drawSettingsSurface` is 262 in
one function.

**What it costs today.** The same shape `TD-23` and `TD-32` name, and the
reason is the same: the function walks the rows advancing a running `y`, and the
pieces it walks with -- the row's boxes, its wrapped help, its height, whether
it is the selected one -- are locals shared by every band it draws, so no band
can be moved out of the function on its own. It is also the file's own
measurement of what fits: `surface.rowsShown` is written here because only the
paint knows how many variable-height rows the pane held, and the wheel and the
arrow keys then clamp against it.

Nothing fails. The file is under the 1,000-line ceiling
`architecture_no_shell_source_is_a_catch_all` enforces and the function is
correct; the cost is that the second-largest source under `src/app/` is one
paint, one click handler, one key handler and the settings store's write path
in one place, and that the next setting added lands in a 262-line function.

**Why it has not been paid.** It is the fourth instance of the carrier problem
-- `DocumentLayout::update`, `rebuildSidebarRows`, `Overlay::draw`, this -- and
`TD-32` already says why they are worth doing together, once, with one shape:
a cursor type that owns the vector, the running `y` and the metrics, with each
band a method on it. Doing this one alone would be a fourth shape. What can be
split out of this file first and independently is narrower and worth naming:
`applySetting` and `resetSetting` are the settings *store's* write path wearing
a paint file's name, and they have no dependency on the surface at all.
