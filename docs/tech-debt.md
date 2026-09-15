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
TD-51 have been used. Closing an entry means deleting it and saying so in the
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

## TD-49 — The Links panel's memo turns on the library revision, so a save is a query

`RightPanelState::library` holds the panel's backlinks and tags, and its key is
`{noteId, catalog().revision()}`. Every save bumps that revision — deliberately,
because the things derived from a note's *body* do change on a save that touches
none of the five fields the note list is built from. So while the Links view is
showing, a save misses that memo and runs `LibraryIndex::backlinks`, which is a
SQLite query and, since TD-48 was paid, is also what runs the note's deferred
index write.

**What it costs.** Measured by `shell.links_panel_over_a_save`, which types a
character and saves, twenty-four times, with the Links view on screen:

| | builds | reused | index transactions |
|---|---:|---:|---:|
| Links view showing, 24 saves | 26 | 24 | **25** |
| Outline view showing, 60 saves (`save.minute_of_typing`) | — | — | **1** |

The keystrokes are free either way; it is the saves that miss. So TD-48's
coalescing — sixty saves into one whole-note store and tokenise — is undone
entirely for a reader with the Links panel open, and a minute of typing goes
back to putting the note through `notes.body` and `notes_fts` sixty times. On
the 200 KB fixture that is 12 MB through each, once a second, which is the
number TD-48's entry opened with.

**Why it has not been paid.** The key is honest about what it is keyed on. The
narrower truth is that a *body-only save of the selected note* cannot change who
links to it: a backlink is another note's `links` row, and the only row the
selected note's own save rewrites is its own. Acting on that means the catalog
carrying two revisions — "the library changed" and "the library changed by
something other than the open note's own body" — and every memo picking the
right one. That is a new thing to get right per memo, and getting it wrong is a
panel showing a stale list with nothing failing to compile, which is the failure
mode this tree has spent several passes removing rather than adding. It is worth
doing when a second memo wants the same distinction, so the split pays for more
than one caller; until then the honest statement is that the Links panel costs
a query per save and the counters say so.

## TD-51 — A note inside a `files` folder stops being a note, and only the status bar says so

`files` is the companion area's name. Everything under one is a *file*: listed
in the tree, opened by the desktop's default handler, and never read, rendered
or indexed — and `library/Library.h` is explicit that a `.md` under one is a
file too. For an attachment that happens to be Markdown that is the right rule
and it is not what this entry is about.

It is about how a reader gets there. `NoteCatalog::createFolder` refuses
`files` as a notebook name, so the reader who wanted one makes the directory in
a file manager instead and puts notes in it. Every one of those notes is then a
companion.

**What it costs.** Measured by `shell_markdown_stranded_in_a_files_folder_is_reported_on_open`:
a library with `Ordinary.md` at the root and `files/Recipe.md` under it has one
note, not two. `Recipe.md` is absent from the note list, absent from
`notes_fts` so a full-text search does not reach it, absent from `links` so
nothing backlinks it and no `[[Recipe]]` resolves to it, and it opens in
whatever the desktop hands a `.md` to rather than in micronotes. The note is
not lost — the bytes are where the reader put them — but every feature the app
has stops applying to it, and the tree draws it beside the notes with nothing
to say which kind it is.

**What has been paid.** The refusal now names the reason rather than saying
"Notebook change failed", so the reader is told what `files` is for before they
go around it. And `openLibraryRoot` counts the Markdown under the files areas
and reports it once, on open, through `library::markdownCompanionCount`.
Together those cover the reader who is about to make the mistake and the one
who already has.

**Why it has not been paid in full.** What is left is that the status line is
a *message*, and a message is seen once and gone in a few seconds. The reader
who opens the library on a Monday morning and looks at the tree sees nothing;
the tree still draws `files/Recipe.md` with a file's affordances and no mark
saying that this particular file would have been a note anywhere else in the
library. Fixing that properly means the sidebar's companion rows carrying a
state — which is a per-row lookup on a path extension in the row builder, and
a visual treatment that has to distinguish "this is an attachment" from "this
is an attachment that you probably meant to be a note" without turning the
tree into a diagnostics panel. That is a design decision with a drawing change
behind it, and it is worth making when somebody hits this a second time. The
honest statement today is that the trap is signposted at both ends and still
open in the middle.
