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

## TD-15 — two caches still flush everything to make room for one entry

`src/app/MarkdownBlocks.cpp` (`complexCache`, at 64 entries) and
`src/ui/Draw.h` (`ImageCache`, at 512).

Both are `if(size() > limit) clear();`. This tree already has a position on that
shape and a test asserting it —
`text_texture_cache_evicts_least_recently_used_not_everything` — because the
glyph cache used to do exactly this, and a full flush discards precisely the
entries about to be reused, since it cannot tell which those are.

**What it costs today.** Bounded, and that is why it is an entry rather than a
fix. A cold open of a note with 120 tables makes 124 md4c parses — one per
block plus a handful, because the flush happens after the last block that will
be laid out. What it costs is the *next* pass over the same note: a resize
relays every block, so a note with more than 64 tables, footnote definitions or
raw-HTML blocks re-parses its way through each width step, and a note with more
than 512 distinct images re-decodes and re-uploads its textures. Neither is a
note anybody has yet complained about.

**Why it is still here.** The right fix is not an LRU — it is the one
`doc::Layout` already uses for the block layouts these mirror: sweep against the
keys the current frame actually asked for, so what survives is what is live
rather than what is recent. That means both caches learning which entries the
layout is holding, which is a channel neither has. An LRU is the cheap version
and would be a second eviction policy in a codebase that has argued itself into
one.
