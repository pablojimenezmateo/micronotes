# Tech debt

Known debt, each with a stable id so a comment in the code can point at it and a
commit message can close it. One entry per thing, and every entry says what it
costs today and why it has not been paid — an entry with no cost and no reason
is a preference, not debt, and does not belong here.

Performance debt that is part of a measured narrative lives in
`docs/performance.md` under its `### Open:` headings; those are cross-referenced
here rather than duplicated, because that file carries the numbers and the
history that make them make sense.

**Adding an entry:** take the next free number, never reuse one. Numbers up to
TD-48 have been used. Closing an entry means deleting it and saying so in the
commit; a register of things that turned out to be fine is a register nobody
reads.

---

## TD-41 — Text in a table cannot be selected on the rendered page

A table is a `BlockKind::Complex` block, and `doc/Layout.cpp` gives such a block
exactly one `TextRun`: zero-width, empty `text`, spanning the block's whole
source range. Both query paths in `doc/LayoutQueries.cpp` skip a run with no
text, so `selectionRectFor` returns nothing for a table's line and `offsetAt`
falls through to the block's content start.

**What it costs.** Three things, and the third is the one a reader notices.
A click anywhere in a table puts the caret at the table's first byte rather
than in the cell that was clicked, so there is no way to select part of one.
A drag that crosses a table paints no highlight over it, so the selection
appears to jump the block. And a drag from above the table to below it does
select the table's bytes -- the offsets are contiguous in the buffer -- so the
copy carries the author's `| --- | --- |` pipes and dashes rather than the words
that were on screen. Pasting a table out of a note into a message means
retyping it.

**Why it has not been paid.** Selection is addressed in *source* offsets, and a
`Complex` block has no map from a pixel inside it back to a source offset: md4c
parses it separately, `app/MarkdownBlocks.cpp` draws the result, and nothing in
between records which bytes of the block a given cell came from. Giving it one
means the complex path producing positioned runs that carry offsets into the
*note's buffer*. `doc/RenderLayout.h` now produces positioned runs, so half of
that is done -- but their offsets index the flattened text it builds, because
md4c's callbacks report no source offsets at all and there is nothing to
address the buffer with. Closing this means either carrying offsets through
md4c's parse or scanning the block's source alongside its parse to recover
them. Until then the block is one addressable position by construction, and a
fix that only painted a highlight over it would be highlighting bytes the copy
does not match.

## TD-44 — A subsetted CFF is cut down, not laid out again

`core/pdf/SfntSubset.cpp` cuts an embedded face down to the glyphs a note
showed, and for the TrueType face that is a real subset: `glyf` and `loca` are
rebuilt and the table shrinks to what was used (`JetBrainsMono-Regular` goes
into a note as 5.7 KB rather than 128 KB). For the CFF face it is not. The
charstring INDEX is rebuilt *inside the byte range it already occupied* —
kept charstrings packed at the front, one `endchar` per dropped glyph, the rest
zero filled — because a CFF's Top DICT holds absolute offsets to its charset,
its encoding and its private DICT, and shortening the INDEX moves all of them.

**What it costs.** About 55 KB of compressed stream per CFF face, where a real
subset would be nearer 10. Measured, on `Inter-Regular` with sixty glyphs
shown: 460 KB of `CFF ` table compressing to 49 KB, made up of the string
INDEX (14.6 KB — the glyph *names*, which a PDF reads glyphs by number and so
never consults), the global subroutines (11.6 KB), the private DICT and local
subroutines (9 KB), and the charset. A one-paragraph note exports at 113 KB
against the 489 KB it was, and against the 30 KB a real subset would give;
a note in four faces at 181 KB against 852 KB. The uncompressed font program
is still 474 KB, which is what a viewer decompresses and holds — nothing has
said that matters, and it is the only cost of the blanking that is not paid.

**Why it has not been paid.** The three remaining pieces each need something
this deliberately does not do. Dropping the unused *subroutines* means
interpreting charstrings to find which ones they call, which is a Type 2
interpreter. Dropping the string INDEX and the charset means rewriting the Top
DICT's offsets, and doing that without changing the DICT's own size means
re-emitting every offset operand in the fixed-width five-byte form — which is
a CFF writer, and the place for it is `core/pdf/CffSubset.cpp`, which is the
only thing that reads the table. The cost the debt named was file size and
three quarters of it is paid; what is left is worth a CFF writer only when
somebody minds the last 50 KB.

---

## TD-45 — The wheel does nothing over the tab strip

A strip with more tabs than fit scrolls, and the only way to scroll it is the
overflow chevrons at either end — each of which steps the *active tab* one
along, because the strip has no scroll of its own to set: the window it shows
is derived from which tab is active (`ui::layoutTabs`). `routeWheel` in
`app/Scroll.cpp` routes the wheel to the sidebar, the right panel and the two
panes by where the pointer is, and the strip is not among them, so a wheel over
the tabs has never done anything at all.

**What it costs.** The one gesture everybody tries on a row of tabs is dead,
and dead silently — there is no feedback that says the chevrons are the way.
Reported as part of "I cannot use the tab strip anymore": the strip really was
broken at the time for an unrelated reason, and the wheel was assumed to be the
same breakage rather than the thing it is, which is a gesture that was never
wired.

**Why it has not been paid.** Not because it is hard but because it is not
clear what it should do. Scrolling the strip means changing the active tab,
which means a wheel over the tabs switches the note being read — surprising in
a way the chevrons are not, since those were pressed on purpose. Doing it
without that means giving the strip a scroll offset of its own and deriving the
visible window from two things instead of one, which is the layout's whole
simplification undone for a gesture. Worth doing when somebody decides which of
those two a wheel over the tabs should mean.

---

## TD-47 — The find bar rescans the whole note on every keystroke

`refreshFindMatches` in `app/FindBar.cpp` memoises the match list on
`(editor revision, needle, options)` and rebuilds it whole on a miss. Typing in
the *note* while the bar is open changes the revision, so every keystroke is a
`util::findAllInto` over the buffer: `shell.find_scan` is 101 us on the 200 KB
fixture, which after the twelfth and thirteenth passes makes it the largest
single thing a keystroke does above the layout — twice the note page and
twenty-five times the raw pane.

**What it costs.** 101 us per keystroke, on top of the 47 us the rest of a
split-view keystroke costs, whenever the find bar is open. It is not visible on
a small note and it is the whole of the cost on a large one, and the shape it
has is the one the reader is most likely to be in: find-as-you-type, editing
what was found.

**Why it has not been paid.** The two passes beside it — the raw pane's wrap
and the status bar's caret — bound their work by `editor::TextEdit`, and the
same is available here: matches wholly before the edit are unchanged, matches
wholly after it shift by `newEnd - oldEnd`, and only the window
`[start - needle.size() + 1, newEnd + needle.size() - 1)` has to be rescanned.
What makes it more than a transcription of those two is that the *result* is
not just a partition to splice. `find.active` is an index into the list and has
to survive the splice pointing at the same match; `find.truncated` is a
`kMaxMatches` cut-off whose position moves when matches are inserted before it,
so an incremental update can flip a note from truncated to not without
rescanning enough to know; and a needle that matches nothing is the common case
while a query is being typed, which the current scan already handles in one
pass with no allocation (`search.query_matches_nothing`, `docs/performance.md`,
the eleventh pass). Worth doing, and worth doing after somebody decides what
`truncated` means under an increment.

---

## TD-48 — Autosave re-stores and re-tokenises the whole note, once a second

Every save goes through `LibraryIndex::refreshWrittenFile`, which upserts the
note's row -- `notes.body` holds the entire buffer -- and then deletes and
re-inserts its `notes_fts` entry, which tokenises the same bytes again. Both
halves scale with the *note*, not with the edit, and autosave runs once a
second for as long as somebody keeps typing.

**What it costs.** Measured on the 200 KB fixture in a 1,000-note library,
with the split timers this entry was opened alongside:

| | per save |
|---|---:|
| `library_index.write_fts` (tokenise) | 1.02 ms |
| `library_index.write_rows` (store the body) | 1.19 ms worst |
| `library_index.refresh_file` self (the `COMMIT` those two fill) | 0.63 ms |
| `library_index.refresh_file` total | **1.24 ms** |
| `save.autosave_note`, the whole path | 2.73 ms |

So the index is about half of what an autosave costs, and the note's own
durable file write is the other half. `library.index_body_bytes_stored` and
`library.index_body_bytes_indexed` are the deterministic half of the same
statement: a minute of typing in a 200 KB note puts 12 MB through each.

It is not a dropped frame -- 2.7 ms once a second against a 16.7 ms frame --
so this is CPU and battery rather than stutter, which is why it is an entry
rather than a fix. It is also the *floor* the eighth pass left: that one took
a save from 18.7 ms to here by removing a whole-library tree walk and three
whole-file reads, and what is left is genuinely the note being written twice.

**Why it has not been paid.** The obvious shape is to write the row's list
fields eagerly and the body and its FTS entry lazily -- flushed before a search
reads them, or when the editor goes idle -- which coalesces sixty index writes
into one. The cost is that `LibraryIndex`'s contract stops being "current after
every write" and becomes "current before every read", and every reader has to
be one that flushes: the search, the backlinks panel, the tag list, the
external-change watcher, and whatever is added next. This tree's stated
priority order is speed, then correctness, then low CPU, and buying CPU with
correctness surface is the wrong way round for a cost that is not a dropped
frame. Worth doing when either the number moves (a bigger note, a slower disk)
or the flush points can be made structural rather than remembered -- a reader
that cannot forget, the way `NoteCatalog` is the one path a note's file is
written through.
