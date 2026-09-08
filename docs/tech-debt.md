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
there: its own soft wrap over `editor::WrappedLines`, its own line stepping, its
own scrollbar arithmetic against the page rect the other two share.

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
