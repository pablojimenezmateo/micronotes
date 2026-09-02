## Why

This is what is left of the `microide-port` proposal. That change listed nine
subsystems across six phases; most of them have since been built, by other
work, in a different order than it planned:

- The three drifting command tables are one registry (`src/ui/Actions.h`), and
  the palette, the `F1` list and the context menus all read it.
- The text cache is a bounded LRU (`src/core/render/TextTextureCache.h`) rather
  than one that cleared itself at 4,096 entries.
- Measurement landed: scoped timing and event counters under
  `src/core/perf/`, with `docs/performance.md` written around them.
- `Application.cpp` was split -- `Chrome`, `Notes`, `PageView`, `RawPane`,
  `RightPanel`, `SettingsDialog`, `TabStrip`, `WikiLinks`, `WindowChrome` --
  and `tests/ArchitectureTests.cpp` holds the line with a ratchet that only
  goes down.
- The verification harness is `CMakePresets.json` plus `tools/run-checks.sh`,
  including a clang warnings-as-errors lane.

Two things it identified were not built, and both are still true today.

**The app cannot see a note change on disk.** `note-library` requires that when
a note file changes outside micronotes, "the system detects the changed file and
updates the SQLite index before presenting stale metadata as current." Nothing
in `src/` watches the library folder; detection happens only when the user
presses `Ctrl+R`. For an app whose whole premise is that the notes are ordinary
`.md` files on disk that other tools may touch, this is the gap that most
contradicts the premise.

**Colours are compiled in.** The light and dark palettes are literals in
`src/ui/Theme.cpp`. There is no way to supply another without rebuilding.

## What Changes

**A file watcher over the library root**, and -- the actual design work -- a
policy for what a detected change means for the index, the sidebar, and an open
buffer. The specs did not previously cover the interesting case: an external
change to a note that has unsaved edits. Ships synchronously first, reusing the
existing `SDL_WaitEventTimeout` idle path; no threads required.

**A theme file format** with include support, discovered from a user themes
directory and derived into the existing semantic `Theme` struct. The current
light and dark palettes ship as built-in themes through the same mechanism
rather than as a special case. Reading a theme file executes nothing: it is
colour data, which is why `product-vision` can admit it while continuing to
exclude plugins.

**Concurrency remains conditional and remains unstarted.** The
`interface-responsiveness` delta is carried forward unchanged: a cancellable
background executor and a main-thread mailbox, undertaken only if the
measurement that now exists shows these paths costing frames on a real library.
It is listed here so the requirement is not lost, not because it is scheduled.

## Impact

- `note-library` gains automatic detection; the manual refresh stays.
- `product-vision` admits a user colour theme file as configuration data.
- New capabilities: `external-change-reconciliation`, `theme-customization`.
- `interface-responsiveness` is carried forward and still conditional.
