## Why

micronotes should provide a fast, local-only Linux notes application for users who want Joplin-style Markdown notes without sync, Electron, mobile support, plugins, web clipper, or feature bloat. This change establishes the initial product contract before implementation starts so storage, rendering, attachments, and UI scope stay intentionally small.

## What Changes

- Add a native Linux desktop application named `micronotes` built with C++20, CMake, and SDL3.
- Store notes in a user-selected local folder library where Markdown files remain the authoritative data.
- Add a SQLite database as a local index/cache for note metadata, tags, folder tree state, and search acceleration.
- Add notebooks/folders, tags, note list, raw Markdown editor, rendered Markdown viewer, and editor/viewer split pane.
- Render a constrained Markdown subset through a vendored in-tree `md4c` dependency, with no Mermaid, no WYSIWYG editor, no webview, and no extension sugar.
- Support attachments stored inside the note library: images render inline in the viewer, and non-image attachments appear as links that open via the Linux default handler.
- Keep the application offline-only with no importers, sync, cloud accounts, plugin marketplace, telemetry, or network access.

## Capabilities

### New Capabilities
- `product-vision`: Defines the micronotes product scope, non-goals, platform constraints, and dependency posture.
- `note-library`: Defines local folder-backed note storage, SQLite index/cache behavior, note identity, and persistence rules.
- `note-organization`: Defines folders/notebooks, tags, note listing, and local search requirements.
- `markdown-workflow`: Defines raw Markdown editing, rendered viewing, split-pane behavior, supported Markdown subset, and viewer constraints.
- `attachments`: Defines attachment storage, Markdown link behavior, inline image rendering, and default-handler opening for other file types.

### Modified Capabilities
- None.

## Impact

- Adds initial C++/SDL3 application structure, CMake build targets, tests, and local runtime directories.
- Adds vendored third-party source for `md4c`; SQLite and SDL3 are expected as system development packages.
- Adds a local library format under the user's filesystem and a rebuildable SQLite cache/index inside the library metadata area.
- Excludes network, sync, import/export compatibility, Joplin data import, plugins, WYSIWYG editing, Mermaid diagrams, and non-Linux platform support from the initial product.
