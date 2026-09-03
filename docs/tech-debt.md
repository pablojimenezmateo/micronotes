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

## TD-1 — `caretRect` walks every row of the caret's block

`src/doc/Layout.cpp`, `DocumentLayout::caretRect`.

Finding the run that holds the caret is a linear walk of every visual row of the
block and every run of every row. For ordinary prose a block is one short
paragraph and this is nothing. A fenced code block is a *single* `SourceBlock`
that can be thousands of rows long, and the caret is drawn once per frame:

| caret position | cost per frame |
| --- | --- |
| prose, mid-note (7,018 blocks) | 0.02 us |
| in a 4,000-line fence, at the start | 0.01 us |
| in a 4,000-line fence, at the end | **7.1 us** |

**Why it is still here.** 7 us is 0.35% of the 2 ms frame budget, and the shape
that provokes it — one enormous fence — is uncommon. Against that, a wrong
caret rectangle is *visibly* wrong, and the fallbacks in that function (the run
containing the offset, else the last run before it, else the block's first row)
are the fiddly part of it.

**What the fix is.** A block's `runs` are monotonically non-decreasing in
`srcStart`: `Flow` emits them in group order, groups are filled in source order,
`splitWord` emits its pieces in order, the hidden opening fence is pushed in
front of the first line's content, and `appendTrailingLine` appends the largest
offset last. So the row can be found by binary search and only that row's runs
walked. It is O(rows) today and O(log rows) after, with the same answer.

The measurement that would justify it: `caretRect` does not have a scope timer.
Give it one before touching it, because 7 us will not show up in
`shell.content`.

## TD-2 — trimming one search snippet costs 0.25 ms

`src/ui/TextUtil.cpp`, `snippetAroundMatch` and `ellipsizeToFit`.

Fitting one matching line into the sidebar's column measures the whole line,
bisects to find how much of the head to give up, and then bisects again inside
`ellipsizeToFit` to trim the tail. Around eighteen measurements, and **every
probe is a string nothing has measured before** — a unique substring of a unique
line — so `TextMeasureCache` cannot help and each probe is a real shaping call.
The probes are long, too: both bisections start from the whole string and work
down, so the early probes measure hundreds of bytes to discover they do not fit.

`sidebar.snippets_trimmed` is the counter.

**Why it is still here.** It used to be paid 600 times in the frame after a
keystroke in the search box, which was a 200-300 ms freeze per character; that
is fixed, by trimming only the rows about to be drawn (d6e8c34). At 36 calls a
query it is ~9 ms of the ~30 ms that frame still costs — worth having, no longer
worth a risky change to text truncation.

**What the fix is.** Predict instead of bisecting from the top. The full-line
measurement is already taken for the early-out, so `width / bytes` gives an
advance estimate; the fitting length is `bytes * maxWidth / width` to within a
few characters for proportional text. Start the bracket there, verify, and widen
only if the estimate was wrong. That turns ~18 long probes into ~3 short ones
without weakening the guarantee, which is that a cut is only accepted once it
has been *measured* to fit.

`TextUtilTests` covers the edges — a match wider than the column, a match at
either end, multi-byte code points — so the change is verifiable.

## TD-3 — the sidebar's draw scans every row to find the visible ones

`src/app/Sidebar.cpp`, `drawSidebar`.

The draw loop walks `ui.sidebarRows` in full and tests each row against the
list rect. Rows are laid out top to bottom in one pass, so `rect.y` is
non-decreasing across the list and the visible band is two binary searches —
which is exactly what `DocumentLayout::blockRange` is for the page, and for the
same reason.

**Cost.** ~400 iterations of a float comparison per frame on a 400-note library
with folders open, to draw thirteen rows. A couple of microseconds. It is listed
because it is O(library) per frame and the library is the thing that grows: at
10,000 notes it is 10,000 comparisons a frame, and `sidebar.rows_drawn` will
still read thirteen.

**Why it is still here.** Two microseconds, and the loop body also assigns
hover and drop-target state that a reader has to check does not depend on rows
outside the band before narrowing it.

## TD-4 — `turnBlocksInto` rescans its chunk once per block

`src/doc/Edits.cpp`, `turnBlocksInto`.

The multi-block turn-into copies the selected span into a `chunk`, then walks
its blocks back to front calling `turnInto` on each — and each of those scans
`chunk` again. O(blocks in the selection) scans of the selection.

**Why it is still here.** Deliberately. Lending the chunk's partition to the
inner calls *would* be safe, because the walk goes back to front and every
rewrite lands at a higher offset than the next iteration reads — but that makes
the correctness of the whole function depend on the loop's direction, with
nothing at the point of the loop to say so. The cost is bounded by the
selection, not by the note, and the scan of the *note* that this function used
to pay for twice over is gone (f19e00c). See the comment in the loop.

**What the fix is.** If it ever matters: rescan `chunk` once per iteration into
a buffer the function owns, rather than lending a stale partition. Same
complexity, no allocation per block.

## TD-5 — the library directory is walked twice on startup

Cross-reference: `docs/performance.md`, "Resolved: the library directory was
walked three times on startup" — the third walk is still open at the end of that
section, and `library.directory_entries_visited` reads 814 for a 401-note
library because of it. The note there also names the deeper duplication: the
index has already read every file and holds each one's id, path and title in
SQLite, and the organization service then opens all of them again for those same
three fields plus tags and icon. Two columns on the index would make the note
list a `SELECT`.

## TD-6 — performance debt tracked in `docs/performance.md`

Not repeated here. Each is a `### Open:` section with its own numbers:

- an edit still touches every block below it (materialised positions vs. a
  Fenwick tree)
- `resolveFolds` is still O(blocks) on every edit *that has a fold in it*
- `SourceBlock` is 88 bytes and holds a `std::string`
- the staging tokens are built only to be thrown away
- the harness cannot see the font path
- `matchEdges` compares the whole buffer to find a one-byte edit
- the live page's hooks are rebuilt every frame
- the cache sweep frees what the next relayout is about to allocate
  (unmeasured)
