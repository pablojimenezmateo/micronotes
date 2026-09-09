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


## TD-34 — nothing scrolls sideways

`app/Scroll.cpp`'s `routeWheel` takes `wheel.y` and the loop hands it nothing
else: `event.wheel.x` is read nowhere in the tree. No surface carries an
x-offset either -- `ui::ScrollList` and `ui::RowStrip` are one axis, and
`PageView` has `scroll()`/`maxScroll()` and no horizontal pair.

**What it costs today.** Content wider than its column cannot be reached. A
table wider than the page, a code line longer than the column and a note title
longer than the sidebar are all clipped and that is the end of it -- the paint
already installs the clip (`PageViewPaint`'s `columnClip` keeps a code block
"inside the column: a long line scrolls off its own right edge rather than out
over the gutter"), which is the right treatment only if there is a way to
follow it. There is not. The reading pane's `00-Index`-style tables are the
common case: a fifteen-row table of links whose last column is unreachable.

A trackpad's horizontal gesture is also silently dropped rather than falling
back to a vertical scroll, so a two-finger swipe that is slightly off-axis
scrolls and one that is on-axis does nothing.

**Why it has not been paid.** It is not one change. Every scrolling surface
grows a second axis, `ui::scrollbarGeometry` becomes two bars with a corner
between them, `ui::RowCursor`/`ui::rowBand` are vertical by construction, and
the wheel router has to decide what a diagonal gesture means. The prior
question is a design one and is worth answering first: a Markdown reader may be
better served by *wrapping* what is too wide -- a table that reflows, a code
block that soft-wraps with a continuation marker -- in which case the axis is
never needed and the clip becomes the bug rather than the containment. Doing
both would be the worst outcome, so the decision comes before the work.

## TD-35 — a resize repaints a frame behind the window

`src/app/Application.cpp`'s loop, and `WindowChrome`'s borderless window.

Every frame is drawn from scratch and presented with VSync on. The window is
`SDL_WINDOW_BORDERLESS` with a custom hit test, so a drag on its edge is an
*opaque* resize the window manager performs: the X window grows, and the region
it grew into holds undefined content until micronotes presents. The loop drains
the resize events, draws once, and presents -- so there is one frame of
unpainted window per resize step, which reads as a flicker to black for the
whole drag.

**What it costs today.** Measured over ten scripted `xdotool windowsize` steps
on a 1000x700 window with a 400-note library: 18 presents, `frame.draw_micros`
42,182 and `frame.present_micros` 194,944 -- so 2.3 ms of drawing per frame
against 10.8 ms of presenting. The draw is not the problem and neither is a
missed frame: the app repaints on every resize step. What is missing is anything
on screen in between.

**Why it has not been paid.** The fix is a retained scene texture: keep the last
frame, blit it immediately when the window is exposed or resized, and draw the
real frame after -- so the window is never unpainted, only briefly stale. That
is what `../microide` does, and it is four files there
(`SceneTexturePresenter`, `ApplicationPresentationCache`, `DirtyRegionPolicy`,
`RedrawTraceAccumulator`) because the retained texture also wants a
dirty-region policy to be worth its memory. micronotes presents the whole
window every frame, so it would get the flicker fix without the partial-redraw
half -- but it is still a new stage in the frame, with its own lifetime against
the renderer, and it belongs in a pass that measures it rather than in one that
notices it.

