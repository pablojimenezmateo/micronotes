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
TD-45 have been used. Closing an entry means deleting it and saying so in the
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
