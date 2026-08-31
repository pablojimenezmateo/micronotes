## 1. Foundations And Visual Restyle

- [x] 1.1 Split `src/app/Application.cpp` into `src/ui/{Theme,Fonts,Draw,Overlay,Fuzzy,TreeModel}` and `src/app/panels/{Sidebar,PageView,StatusBar,Overlays}`, leaving only `run()`, the event pump, and wiring behind. No behavior change.
- [x] 1.2 Replace the hardcoded `SDL_Color` constants with a theme token layer (color roles, type scale, spacing scale, radii) and add light and dark palettes.
- [x] 1.3 Persist the selected theme in `.micronotes/ui.state`.
- [x] 1.4 Vendor OFL UI and monospace font files under `third_party/fonts/`, install them through CMake, and load by path with a system-font fallback.
- [x] 1.5 Honor `SDL_GetWindowDisplayScale`: rebuild fonts and clear the text cache on scale change, and key the text cache by size.
- [x] 1.6 Apply the measured content column, page title as a rendered heading, and revised whitespace.
- [x] 1.7 Build the overlay/popover stack with keyboard capture and dismissal rules.
- [x] 1.8 Move rename, new-folder, and tag editing out of the status bar into overlay popovers.

### Phase 1 notes

Landed alongside the foundations, because the restyle exposed them:

- [x] 1.9 Fix soft line breaks rendering as hard breaks, per the contract stated in `docs/markdown-elements.md`.
- [x] 1.10 Fix inline layout inventing a space after a link and dropping one across run boundaries.
- [x] 1.11 Give notes without front matter a stable derived id so externally-created `.md` files can be opened, searched, and adopted on first save.

## 2. Live Markdown Surface

- [x] 2.1 Implement `src/doc/BlockScan` with exact source ranges and `Complex` tagging for tables, HTML blocks, and footnotes.
- [x] 2.2 Add tests proving scanned blocks partition the buffer with no gaps or overlaps.
- [x] 2.3 Implement `src/doc/InlineScan` recording marker ranges separately from content ranges.
- [x] 2.4 Implement `src/doc/Layout` with `caretRect`, `offsetAt`, `selectionRects`, `blockAt`, and `rowRelative`.
- [x] 2.5 Add per-block layout memoization keyed by content fingerprint, width, font scale, and marker visibility.
- [x] 2.6 Add layout round-trip tests: `offsetAt(caretRect(o)) == o` for every offset in a fixture document.
- [x] 2.7 Render the live surface in `PageView`: caret, click-to-place, drag-select, scrolling, find highlighting.
- [x] 2.8 Reveal syntax markers only in the block holding the caret.
- [x] 2.9 Render `Complex` blocks through the `md4c` render model, with raw-text editing on demand.
- [x] 2.10 Add mode switching: live (default), raw, reading, and split.
- [x] 2.11 Extend `micronotes_perf` with layout and re-layout benchmarks and assert the keystroke budget.

## 3. Editing Behavior

- [x] 3.1 Add `MarkdownEditor::replaceRange` and route all block edits through it.
- [x] 3.2 Add undo coalescing for typing runs with structural edits as group boundaries.
- [x] 3.3 Add word-wise movement, shift-selection, `Ctrl+Home`/`Ctrl+End`, `PageUp`/`PageDown`, and `Ctrl+Backspace`.
- [x] 3.4 Implement `src/doc/Edits` transforms: `turnInto`, `moveBlock`, `duplicateBlock`, `deleteBlock`, `indent`, `outdent`, `toggleTodo`, `wrapSelection`, `continueList`.
- [x] 3.5 Add Markdown typing shortcuts for headings, bullets, ordered items, todos, quotes, fences, and dividers.
- [x] 3.6 Add list behavior for Enter, Tab, Shift+Tab, and Backspace at item start.
- [x] 3.7 Add clickable task checkboxes and `Ctrl+B`/`Ctrl+I`/`Ctrl+E`/`Ctrl+K` formatting.
- [x] 3.8 Add tests for each transform and for byte-exact undo restoration.

### Phase 2 notes

On 3.5: the live surface renders the buffer's own Markdown, so `# `, `- `,
`1. `, `> ` and `---` become headings, lists, quotes and dividers the moment
they are typed - there is nothing for a shortcut to rewrite. Only two shapes
need one, and both are implemented: `[] ` / `[ ] ` / `[x] ` becomes a real task
marker, and Enter on an unclosed fence adds the closing fence.

## 4. Block Affordances

- [x] 4.1 Add the gutter drag handle and insert button on block hover.
- [x] 4.2 Add drag-to-reorder with a drop indicator.
- [x] 4.3 Add the block menu overlay: turn into, duplicate, delete, move to.
- [x] 4.4 Add the slash menu with fuzzy filtering.
- [x] 4.5 Add block multi-select via Esc, Shift+click, and Shift+Up/Down.
- [x] 4.6 Add the floating selection formatting toolbar.

### Phase 3 notes

On 4.3, "move to": moving a block to another *note* needs the command palette
and the multi-note plumbing that Phase 5 brings, so the menu ships the two moves
that are meaningful inside one note - move up and move down - alongside turn
into, duplicate and delete. The cross-note move belongs with 6.6.

Every affordance drives the same `src/doc/Edits` transforms as the keyboard, so
a drag, a menu entry and a shortcut produce the same bytes and the same single
undo step. The range variants (`deleteBlocks`, `duplicateBlocks`,
`turnBlocksInto`, `moveBlocks`, `moveBlocksTo`, `insertBlockAfter`) carry the
blank line that separated a block from its neighbour; without that two
paragraphs merge into one the moment they change places.

The content column now reserves a left gutter for the hover controls: it stays
centred when the page is wide enough and is pushed right when it is not.

## 5. Block Types

- [x] 5.1 Add callout blocks.
- [x] 5.2 Add toggle lists with persisted fold state.
- [x] 5.3 Add divider and restyled quote blocks.
- [x] 5.4 Add code block chrome with a language label and copy button.

### Phase 4 notes

On 5.2, "toggle lists": there is no honest plain-Markdown encoding for a
toggle - `<details>` is HTML, and the scanner hands HTML to md4c - so a toggle
is not a block type here. It is structure the file already states: a heading
owns its section down to the next heading of its rank, and a list item owns the
items indented beneath it. Anything that owns something can be collapsed, and
`BlockKind::Toggle` is gone, since nothing ever produced it.

Folding therefore never touches the buffer. `DocumentLayout` gives a hidden
block zero height and no lines while it keeps its offsets and its place in the
partition, so nothing below it shifts and the caret has no line there to land
on; if the caret ends up inside a fold anyway, the fold gives way rather than
strand it. The collapsed set lives in `.micronotes/folds.state`, keyed by the
block's shape and text rather than its offset, so an edit above a folded
heading cannot detach its fold or hand it to whatever landed there instead.

On 5.1 and 5.3: consecutive `>` lines stay separate blocks - splitting them
would cost the partition invariant - but they are drawn as one container, so a
multi-line quote gets one bar and a multi-line callout one box. The five GitHub
alert kinds each carry their own accent, mixed over the page rather than added
as ten more theme tokens. The reading view was retagging nothing, so `> [!NOTE]`
rendered as literal text there; `promoteAlerts` now turns those quotes into the
admonitions the reading view already knew how to draw.

Found while driving it: Enter on an empty list item or quote line dropped the
marker but left no blank line, and "- one\ntext" is a lazy continuation - the
file said the text was still inside the item while the screen said it was not.
Fixed in `continueList`.

## 6. Navigation Shell

- [x] 6.1 Build the folder and note tree model with persisted expansion state.
- [x] 6.2 Render the sidebar tree with disclosure controls and demote tags to a secondary filter.
- [x] 6.3 Add drag-to-reparent for notes and folders with drop indicators.
- [x] 6.4 Add the `icon` front matter key and preserve unrecognized front matter keys on save.
- [x] 6.5 Add note icon rendering with a monochrome fallback when color glyphs are unavailable.
- [x] 6.6 Add the command palette for note jumping and command discovery.
- [x] 6.7 Add breadcrumbs, recents, and favorites.
- [x] 6.8 Add delete confirmation, `.micronotes/trash/`, and restore.

### Phase 5 notes

On 6.4, the data-loss bug: micronotes rewrites the whole front matter block on
every save, so any key it could not parse was destroyed by the first autosave
after a note written elsewhere was opened. `parseMetadata` now reads front
matter as entries - a `key:` line plus the indented lines that continue it - and
carries every entry it does not model through verbatim and in order. It also
learned YAML's block and flow forms of `tags:` and writes back whichever form it
read, so a note that arrived with `tags:\n  - one` is not silently reflowed.

On 6.1 and 6.2: expansion is a view preference, so it lives in
`.micronotes/tree.state` beside the fold state and never touches a file. The
tree, the favorites, the tag filter and the recents are built into one flat row
list with one geometry, so the draw, the hit test, the arrow keys and the drop
target cannot disagree about what is where. The library root is a real row
rather than an implicit container, which is what makes selecting it and dropping
onto it work like any other notebook.

On 6.5: SDL3_ttf 3.2.2 cannot scale a colour emoji font. Noto Color Emoji
carries one 128-pixel bitmap strike and returns it whatever size is asked for,
so attaching it as a fallback for body text paints an emoji over the four lines
around it - which it had been doing, unnoticed, since before this phase. An
emoji face is now attached to the text faces only if it honours the size it was
opened at, and the colour face is opened separately for the icon column, which
renders the glyph at its own size and scales the texture into its box.

On 6.6: the plan asked for `Ctrl+K` to jump. Phase 2 had already given `Ctrl+K`
to "make a link from the selection", which is what it means in every other
editor, so the jump is `Ctrl+P` and the full command palette is `Ctrl+Shift+P` -
and `Ctrl+K` still jumps when the focus is not the editor, where there is no
selection to link. The palette also closes the loose end left by 4.3: "move
selected blocks to note..." names a target note, appends the blocks there, and
deletes them here as one undoable edit.

On 6.8: deletion was going to the desktop trash, which micronotes cannot read
back, so restoring was not something it could offer. Notes and notebooks now
move to `.micronotes/trash/` with an index recording where each came from;
restore puts it back, brings its attachments with it, and renames around
anything that has taken its name in the meantime.

Found while driving it: an overlay row placed its detail text a fixed 60 pixels
left of its shortcut, which only works while the shortcut is short - a deletion
timestamp printed straight through it. Both now lay out right to left against a
running edge.

## 7. Polish And Documentation

- [x] 7.1 Add the settings overlay for theme, font size, content width, and library path.
- [x] 7.2 Add the keyboard shortcut cheatsheet overlay.
- [x] 7.3 Refresh empty states across the shell.
- [x] 7.4 Update `docs/markdown-elements.md`, `docs/build.md` runtime controls, and `README.md`.

### Phase 6 notes

On 7.1: settings are a list overlay whose rows open the list of their own
values, rather than a panel of bespoke widgets - the keyboard, the filter and
the dismissal rules are then the ones already learnt everywhere else, and the
settings list comes back with the new value on it so changing two things does
not need the dialog opened twice. Text size and page width are named steps
(`small`/`medium`/`large`, `narrow`/`medium`/`wide`) rather than free numbers,
because three steps are one keystroke to pick and a hand-edited `ui.state`
cannot leave the app in a size nobody can read. Text size multiplies the whole
type scale together, so headings keep their proportion to body text, and the
layout cache already keys on the type metrics, so nothing had to be invalidated
by hand. Both live in `.micronotes/ui.state` beside the theme, which also means
two libraries can be typeset differently.

Choosing a library folder opens it in place. `run()`'s startup sequence and its
shutdown persistence became `openLibraryRoot` and `persistLibraryState`, so the
dialog takes exactly the path a launch takes; the library being left is written
out first. `AppState::loadUiState` now clears the view state before it opens the
file rather than after, or a library with no state of its own would have
inherited the favorites and the open note of the one just closed.

On 7.2: the shortcut list is one table, with section headings listed as disabled
items so the arrows step over them and Enter cannot land on one. The keys go in
the `detail` column, which the fuzzy filter searches, so typing `alt` finds
`Alt+Up`. The status bar no longer names a dozen keys it had no room for: three
anchors and `F1`.

Both needed overlay lists to scroll, which they never had - `layoutFor` laid out
the first twelve rows and stopped, so the command palette's last seven commands
were unreachable and arrowing past row twelve lost the highlight off the bottom.
Lists now scroll to follow the highlight, take the wheel, show a thumb, and size
themselves to the window rather than running off the bottom of a short one.

Found while driving it:

- A text prompt drew its value unclipped, so a library path was painted straight
  across the panel and the window behind it. Fields now scroll to keep their end
  in view and clip to their own rect - which every prompt needed, not just this
  one.
- `metadataHeader` wrote a `tags:` line unconditionally, so the first autosave
  after opening a note that had no tags added an empty key to it. Same bug
  family as the unknown keys fixed in Phase 5, and the same fix: omit the key
  when there is nothing to write.
- A hand-wrapped list item was several blocks. `scanBlocks` gave a list item one
  line and let the paragraph rule pick up the continuation, so the live surface
  drew a line break and the continuation's own indentation into the middle of a
  sentence, where the reading view flowed it correctly. An item now owns its
  continuation lines on the same terms as a paragraph, and `startsBlock` learnt
  the rule the scan loop already applied - that a list marker four columns in is
  a nested item rather than indented code - so the two cannot disagree about
  where an item ends.
- A folded line ending still measured as many spaces as it had bytes. All but
  its last byte are now emitted hidden: zero width, still addressable, so the
  runs stay byte-aligned with the source. That costs the exact
  `offsetAt(caretRect(o)) == o` round trip inside those bytes - several offsets
  now share one position - so `LayoutTests` says precisely where the identity
  does not hold instead of asserting something untrue.
