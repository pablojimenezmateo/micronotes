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

Not repeated here. Each is a `### Open:` section with its own numbers:

- an edit still touches every block below it (materialised positions vs. a
  Fenwick tree) — written down as the thing to reach for *if the shift shows
  up*, not as a fix waiting to happen
- `resolveFolds` is still O(blocks) on every edit *that has a fold in it*
- `SourceBlock` is 88 bytes and holds a `std::string`
- the staging tokens are built only to be thrown away
- the cache sweep frees what the next relayout is about to allocate
  (unmeasured, and it undoes a decision that file already justified: measure
  `peak_rss` before touching it)

## TD-9 — the reading pane is still a second renderer for the same Markdown

`src/app/ReadingPane.cpp`.

The live surface renders through `doc::BlockScan` and `doc::Layout` into
`PageView`. The reading pane renders the same note a second time, from a
separate md4c `markdown::Document`, with its own geometry: its own indent step,
its own quote gutter, its own callout box, its own task checkbox, its own code
block, its own table, its own image scaling.

**What it costs today.** Divergence, and only divergence — the *speed* half of
this is paid. The pane is memoised per block, bands its draw to the viewport,
and no longer re-wraps a block to recover a height its own layout recorded, so
it costs 0.37 ms a frame on a 242 KB note where it used to cost 7.6 ms
(`docs/performance.md`, "Resolved: the reading pane measured the whole note
twice a frame").

What is left is that a change to how anything looks has to be made twice, and
a screenshot of one 675-byte note against the live surface found **six** places
where the two had already drifted:

- wikilinks were raw `[[brackets]]`, unstyled and unclickable — fixed
- a ticked task was not struck through or muted — fixed
- inline `code` was drawn in `theme().warn` with no box — fixed
- a callout's kind was title-cased on one surface and lower-cased on the other
  — fixed in `ui::calloutLabel`
- **a callout's shape.** The live surface gives it a title band
  (`kCalloutTitleHeight`), rounded corners and the label on its own line; the
  reading pane runs the label inline with the first sentence. Still open: the
  band is reserved by `doc::Layout` and the reading pane has no equivalent
  place to reserve one.
- **a code block's language.** The live surface draws the info string
  (`cpp`) at the block's trailing edge; the reading pane draws no chrome at
  all. Still open.

Two more that are properties of the two engines rather than of either draw:
**list leading differs** (28 px against 26 px for the same body text, because
`blockLineStep` and `doc::TypeMetrics::lineHeightRatio` round differently), and
the reading pane draws no caret, gutter or block toolbar — which is the whole
point of it, and the reason the merge below ends by turning three flags off.

Four fixed and four open is the argument for the merge, not against it: the
four that were fixed were fixed one at a time, by someone comparing screenshots.

**Why it is still here — and this is the part the previous entry got wrong.**
The old entry claimed "the live surface already draws everything the reading
pane draws, minus the caret, the gutter handles and the block toolbar". That is
not true, and pointing the reading pane at `PageView` today would *lose*
features:

- **Images.** `doc::InlineScan` treats `![alt](target)` as a link-styled text
  run; the live surface never draws the bitmap. The reading pane loads the
  texture, fits it to the column and to 55% of the page height, and falls back
  to a placeholder line. Merging means `doc::Layout` learning that a block's
  height can be a *texture's* height, and `PageView` learning to draw one —
  which also means images start appearing while the note is being edited, which
  is a product decision and not a refactor.
- **Anchors.** An in-note `[#heading]` link and a footnote reference jump by
  scrolling to a recorded offset. The reading pane's memo carries that map;
  `PageView` has no notion of it.
- **Footnote definitions** get a `[label]` drawn in the gutter, and md4c's
  ordered-list numbering (`orderedNumber`) is honoured where the live scanner
  derives its own.

**What the fix is, in order.** Give `doc::Layout` an image block — a measured
height from a hook, the way `measureComplex` already works for tables — and
`PageView` the draw for it. Then anchors, as a block-index-to-top query the page
can answer. Then the reading pane becomes `PageView` with the caret, the gutter
and the toolbar off, all three of which are already conditional on focus or
hover, and the file deletes.

## TD-12 — the reading pane measures every word it draws, every frame

`src/app/InlineText.cpp`, `drawInlineRuns`.

The pane's layout is memoised per block now (`ViewerLayout::bodyHeight`), so
nothing is *measured* to decide a height any more. But the draw still walks
every word of every visible block and measures it to place it: `layoutWords`
tokenises the runs, and each token is measured to advance the pen and again to
decide the wrap.

**What it costs today.** 11,674 text measurements over 60 frames on a 675-byte
note, against the live surface's 3,266 for the same note in the same window.
That is down from 21,339 and the measurements are cache hits — the width cache
is 65,536 entries and its hit rate here is 99.1% — so what is left is hash,
probe and function-call overhead rather than shaping. It is the reason the
reading pane's `shell.content` is still several times the live page's.

**Why it is still here.** The live surface does not have this problem because
`doc::Layout` caches the laid-out *runs* per block — position, width and style
— and redraws them without measuring anything. Giving the reading pane the same
thing means caching a `std::vector<TextRun>` per block against
(block, textWidth, fontScale), which is most of what `doc::Layout` already is.
Doing it here would be building a second copy of that cache in the file TD-9
wants deleted.

**What the fix is.** TD-9. Until then the ceiling is what it is, and the
measurements are cheap enough that the pane makes its frame budget.

## TD-14 — three panes still lay their own text out three ways

`src/app/PageView.cpp`, `src/app/ReadingPane.cpp`, `src/app/RawPane.cpp`.

They now agree on the page rect and the measure (`ui::pageRectIn`,
`ui::pageColumnIn`) and on the scrollbar, which is what made the *geometry*
comparable. What they do inside it is still three engines: `doc::Layout`'s
token flow, `app::layoutWords`'s word wrap, and `RawPane`'s own soft wrap over
`editor::WrappedLines`.

**What it costs today.** Every typographic decision is three decisions. The
line-leading difference in TD-9 is an instance of it, and so was the wrap
difference that was TD-13 -- which had to be fixed in `doc::Layout` alone,
because the reading pane's own tokenizer never had it.

**Why it is still here.** TD-9 is the first two thirds of it and is a project
in itself. `RawPane`'s own header says it is "kept apart so that replacing it
is a matter of deleting one file", which is the right plan and not one to start
in the middle of.
