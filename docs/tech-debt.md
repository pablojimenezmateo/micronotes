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

**Why it is still here.** `RawPane`'s own header says it is "kept apart so that
replacing it is a matter of deleting one file", which is the right plan. What it
is waiting for is a decision rather than a refactor: whether a pane that shows
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
