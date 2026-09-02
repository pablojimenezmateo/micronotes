# Tasks

## 1. Draw primitives and palette

- [x] 1.1 Rounded fill and stroke in `src/ui/Draw.{h,cpp}`, span-based so a
      radius costs one `SDL_RenderFillRects` rather than a cached texture.
- [x] 1.2 Radius, ribbon and tree-guide tokens in `src/ui/Metrics.h`.
- [x] 1.3 Repalette `src/ui/Theme.cpp`: purple accent on one hue, and the
      chrome away from the page in both modes -- which inverts dark, where the
      sidebar used to be the lighter of the two.
- [x] 1.4 Contrast: `theme_palettes_stay_legible_in_both_modes` already held
      every text role to its ratio and holds the new palette to the same, so
      the requirement needed no new test, only a palette that passes it.

## 2. Component skin

- [x] 2.1 Callout title: `src/doc/Layout.cpp` marks the `> [!KIND]` line as the
      run's title, and `PageView` draws a mark and the kind's name on it.
- [x] 2.2 Code blocks on a rounded surface, no border, with the copy control
      appearing only while the pointer is on the block it copies.
- [x] 2.3 Obsidian's checkbox shape, and a ticked task struck through and muted.
- [x] 2.4 Flatter tab strip: chrome ground behind, page ground in the active
      tab, rounded at the top, and no rules between tabs.
- [x] 2.5 Layout tests for the callout title: only the head of a run, never a
      revealed one, never a quote that names no kind.
- [x] 2.6 The same skin in the md4c reading view, so a callout does not change
      shape when the note is read instead of edited.

## 3. Activity ribbon

- [x] 3.1 Ribbon column in `src/ui/ShellLayout.{h,cpp}`, outside the sidebar,
      with its width taken off the top of the panel arithmetic.
- [x] 3.2 `src/app/Ribbon.{h,cpp}`: drawn marks, hit rects, and tooltips naming
      the action and its keys. Every control is an `ActionId` and nothing else.
- [x] 3.3 Panel toggles at the foot, lit when their panel is showing, so hiding
      everything is recoverable without the palette.
- [x] 3.4 ShellLayout test: the ribbon keeps its width and position at every
      window size and panel combination, and no region goes negative.

## 4. Inline title and properties

- [x] 4.1 `src/ui/NoteProperties.{h,cpp}`: front matter regrouped into rows.
      In core rather than beside the draw code, so the grouping rule is tested.
- [x] 4.2 `src/app/PageHeader.{h,cpp}`: the title and those rows, drawn in the
      page's scrolling space so they scroll away with the note.
- [x] 4.3 Click the title to rename in place, through the existing rename
      action and the field machinery every other input already uses.
- [x] 4.4 Tests: `tests/NotePropertiesTests.cpp` covers tags-as-chips, scalars,
      block sequences, indented continuations, an orphaned continuation, and a
      note with no front matter at all.

## 5. Sidebar and status bar

- [x] 5.1 Nesting guides down the tree, and rounded row fills in place of the
      fill-plus-accent-strip selection.
- [x] 5.2 Status bar: word and character counts and the pane mode on the right,
      transient status on the left. The key legend is `F1`'s job.
- [x] 5.3 Counted on the frame that draws them, under a scope timer and the
      `status.word_counts` counter, so the cost is measured rather than assumed.

## 6. Shortcuts

- [x] 6.1 `ActionSpec::altChord`, and `Ctrl+O` on the note switcher.
- [x] 6.2 Tests: no two bindings claim the same keys, aliases included, and an
      alias round-trips through the one chord spelling.
- [ ] 6.3 `Ctrl+E` is Obsidian's edit/read toggle and this app's inline-code
      binding. Left alone: taking `Ctrl+E` from inline code would remove a
      habit in the course of adding one, which is the opposite of what an alias
      is for. Needs a decision, not a patch.

## 7. Verification

- [x] 7.1 `tools/run-checks.sh all` -- tests, then asan, ubsan and tsan.
- [x] 7.2 Both themes screenshotted, in the live surface and the reading view,
      with and without panels.
- [x] 7.3 Frame budget unmoved. `micronotes_perf`, Release, runs interleaved
      between a build of `HEAD` and one of this change to cancel drift:
      `layout.keystroke_relayout_median` 770us before, 760us after, over eight
      pairs, against a 2000us budget. Run all of one binary and then all of the
      other and the change appears 8% slower -- which is the machine warming
      up, not the code, and is why the runs are interleaved.
- [ ] 7.4 `status.count_buffer` is armed but unread on a real library: the
      harness is headless and never draws a status bar, so the one number this
      change adds to a frame has been reasoned about and not yet measured.
