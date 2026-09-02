# Tasks

## 1. Library watching

- [ ] 1.1 Add a watcher over the library root that reports created, modified,
      removed and renamed `.md` files and directories, comparing observed state
      rather than consuming events.
- [ ] 1.2 Drive it from the existing idle path: feed its next-poll delay into
      the `SDL_WaitEventTimeout` the main loop already makes. No threads, and
      idle CPU stays at zero because the loop still blocks.
- [ ] 1.3 Test that an unchanged library produces no work and no wakeups
      beyond the poll itself.

## 2. Reconciliation policy

- [ ] 2.1 Closed note changed on disk: update the index entry and the sidebar
      row, silently.
- [ ] 2.2 Open note, clean buffer: reload and preserve the caret position.
- [ ] 2.3 Open note, dirty buffer: keep the edits, reload nothing, and report
      the conflict on the status line. One behaviour, one place.
- [ ] 2.4 Open note deleted on disk: keep the buffer and allow writing it back.
- [ ] 2.5 Folder created or removed: rebuild the affected part of the tree and
      the index.
- [ ] 2.6 Tests for each of the above, including the interleaving where a
      change lands while a save is in flight.

## 3. Theme files

- [ ] 3.1 Define the file format: semantic role names, colour literals, and
      `include` for building on another theme.
- [ ] 3.2 Add a parser and a derivation layer in front of the existing `Theme`
      struct, changing no call site.
- [ ] 3.3 Move the built-in light and dark palettes onto the same derivation,
      so a built-in is not a special case.
- [ ] 3.4 Discover user themes from a themes directory; a malformed file
      reports where it failed and leaves the current theme in place.
- [ ] 3.5 Contrast: a hard test failure for built-ins, a reported warning for
      user themes, using `src/ui/ColorMath.h`.

## 4. Concurrency (conditional -- do not start without evidence)

- [ ] 4.1 Only if the counters in `src/core/perf/` show the library scan, index
      rebuild, search or the watcher's walk costing frames on a real library.
- [ ] 4.2 Cancellable single-worker executor, main-thread mailbox with per-key
      coalescing, and a checked SDL wake path that cannot strand a result.
