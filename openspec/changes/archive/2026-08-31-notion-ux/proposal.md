## Why

micronotes is fast and correct, but it presents as a developer tool: a flat
notebooks/tags sidebar, a monospace raw-Markdown buffer beside a read-only
preview, rename and tag editing typed into the status bar, a single hardcoded
dark palette, fixed-size hardcoded fonts, no HiDPI scaling, and an editor that
lacks word-wise movement, shift-selection, and page navigation.

This change adopts the interaction model of Notion — direct editing of formatted
content, block affordances, a command palette, and a real navigation tree —
while keeping every existing product constraint: local-only, offline, no sync,
no accounts, no telemetry, and plain `.md` files as the authoritative data.

Two current requirements block this work and must be revised: `product-vision`
excludes WYSIWYG editing under Minimal Scope, and `markdown-workflow` mandates a
raw Markdown editor as the editing surface.

## What Changes

- Add a live Markdown editing surface where formatting renders in place and
  syntax markers reveal only in the block holding the caret. Raw editing and
  rendered reading remain available as explicit modes.
- Add an offset-preserving document scanner that describes the buffer without
  rewriting it, so `.md` files stay byte-for-byte authoritative. `md4c` keeps
  ownership of the reading view and of block types the scanner does not model.
- Add block affordances: hover gutter handle, drag-to-reorder, block menu,
  slash-command inserter, block multi-select, and a selection formatting toolbar.
- Add Notion-style typing shortcuts and list behavior, and close the existing
  editor keyboard gaps.
- Add callouts, toggle lists, dividers, and code blocks with a language label.
- Replace the flat sidebar with a collapsible tree of folders and notes, with
  per-note icons and drag-to-reparent.
- Add a command palette for note navigation and command discovery.
- Add light and dark themes on a design-token layer, vendored UI and monospace
  fonts, and HiDPI display-scale support.
- Replace status-bar text entry with real overlay popovers and dialogs.
- Add delete confirmation and a recoverable trash.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `product-vision`: Minimal Scope no longer excludes WYSIWYG editing; excludes
  per-block colors, column layouts, and database views instead. Adds a
  presentation requirement for themes and display scaling.
- `markdown-workflow`: The editing surface becomes live Markdown, with raw and
  reading modes retained. Adds requirements for source fidelity, block
  interactions, and typing shortcuts.
- `note-organization`: The sidebar becomes a collapsible tree; adds note icons,
  command-palette navigation, and recoverable deletion.

## Impact

- Adds `src/doc/` (block scanner, inline scanner, layout, block edits) and
  `src/ui/` presentation modules; splits `src/app/Application.cpp`.
- Adds vendored OFL font files under `third_party/fonts/`. No configure-time or
  build-time fetching, so the offline-build invariant is preserved.
- Extends note front matter with an `icon` key and preserves unrecognized front
  matter keys that are currently discarded on save.
- Adds `.micronotes/trash/` inside the library metadata area.
- Does not change the library format, the SQLite index, attachment handling, or
  the `md4c` render model.
