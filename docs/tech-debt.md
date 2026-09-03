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
- the harness cannot see the font path
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
this is paid. The pane is memoised per block and bands its draw to the viewport
now, so it costs 0.37 ms a frame on a 242 KB note where it used to cost 7.6 ms
(`docs/performance.md`, "Resolved: the reading pane measured the whole note
twice a frame"). What is left is that a change to how a callout, a quote or a
list marker looks has to be made twice, and the two copies have already drifted
three times: an Html block's bottom spacing, an image's rounding, and the
callout label's case. The first two are fixed by there being one walk; the third
is fixed by `ui::calloutLabel`, and the mark's geometry by
`ui::kCalloutMarkSize` / `kCalloutMarkInset`. Those are three shared numbers
against a whole second renderer.

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

## TD-10 — `ClipGuard` does not nest

`src/ui/Draw.h`.

`ClipGuard`'s constructor sets the renderer's clip rect and its destructor sets
it to `nullptr` — it clears the clip rather than restoring whatever was there
before. So an inner guard's destructor drops an outer guard's clip, and every
draw after that point in the outer scope is unclipped.

**What it costs today: nothing, by luck.** The nested sites happen to be safe.
`drawSidebar` holds a guard for the panel and a second for the list, and the
second outlives every draw that needed the first. `drawReadingPane`'s empty
message takes one inside the page's, and returns immediately after. That is a
property of today's call sites, not of the code: the next `ClipGuard` written
inside another one, with anything drawn after it, silently paints outside its
pane — and the symptom is text over the tab strip, which reads as a layout bug
rather than a clipping one.

**Why it is still here.** It has never bitten, and the fix wants care about what
"restore" means: SDL's clip is a single rect, so a correct guard has to
`SDL_GetRenderClipRect` in the constructor and put that back — and an inner
clip should arguably *intersect* the outer one rather than replace it, which is
a behaviour change at the two sites that currently rely on replacement.

**What the fix is.** Save and restore in the guard, and intersect on
construction. Then check the two nesting sites still draw what they drew: a
`cmp` of two screenshots is the whole test.

## TD-11 — nothing under `src/app/` can be tested

`CMakeLists.txt`.

`micronotes_tests` links `micronotes_core`, which is `src/core`, `src/doc`,
`src/library` and most of `src/ui`. Everything under `src/app/` is compiled only
into the `micronotes` executable, so no test can reach it.

**What it costs today.** `src/app/` holds `PageView` (881 lines), the sidebar
model, the reading pane, the tab strip, the frame policy and `Application.cpp`
itself — and none of it has a unit test. The consequences show up as
workarounds: `sidebarRowRange` is a four-line adapter over `ui::rowBand` in
`src/ui/` purely so the search behind it could be tested at all, and the
reading pane's correctness is checked by comparing screenshots because there is
no way to call it. Both are the right shape for other reasons; neither should
have been *forced*.

**Why it is still here.** `src/app/Shell.h` pulls in SDL3, `PageView.h`,
`ui/Draw.h` and `ui/Overlay.h`, so moving a file to the tested library moves
that world with it — and `UiRuntime` holds an `AppState` by value, which a test
would have to be able to construct.

**What the fix is.** Two steps, and the first is worth doing alone: add the
`src/app/` files whose only dependency is data (`SidebarModel.cpp`,
`FramePolicy.cpp`, `Scroll.cpp`, `Tabs`-shaped things) to the library, and see
what actually fails to link. The second is separating `UiRuntime` from the
drawing headers, which is the same decomposition `Application.cpp`'s line budget
is already pushing.
