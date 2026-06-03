## 1. Project Skeleton

- [x] 1.1 Add CMake project configuration for C++20, SDL3, SQLite, optional SDL3_image/SDL3_ttf discovery, warnings, tests, and Linux-only build assumptions.
- [x] 1.2 Create initial source layout for app bootstrap, platform helpers, persistence, library model, Markdown parsing/render model, editor, viewer, attachments, UI shell, and tests.
- [x] 1.3 Add application entry point that opens a single SDL3 window, initializes rendering/text backends, and exits cleanly.
- [x] 1.4 Add basic test runner target and smoke tests for platform/runtime path helpers.

## 2. Vendored Markdown Dependency

- [x] 2.1 Vendor `md4c` source into `third_party/md4c` without configure-time network fetching.
- [x] 2.2 Wire vendored `md4c` into CMake as a local library target.
- [x] 2.3 Implement a Markdown parser adapter that converts `md4c` callbacks into an internal block/inline render model.
- [x] 2.4 Add parser fixture tests for headings, paragraphs, emphasis, lists, links, fenced code blocks, blockquotes, tables where supported, images, and Mermaid-as-code behavior.

## 3. Library Storage And SQLite Index

- [x] 3.1 Implement library root validation and app metadata directory creation inside the selected library root.
- [x] 3.2 Define note metadata format with stable note id, title, tags, timestamps, and attachment references.
- [x] 3.3 Implement note creation, loading, saving, renaming, moving, and deletion with `.md` files as authoritative note bodies.
- [x] 3.4 Implement SQLite schema, migrations for pre-release schema versions, and cache rebuild from library files.
- [x] 3.5 Implement stale-file detection using path, mtime, size, and note id reconciliation.
- [x] 3.6 Add storage tests for cache deletion/rebuild, external file edits, rename identity preservation, and path boundary rejection.

## 4. Organization And Search

- [x] 4.1 Implement folder/notebook tree scanning from library directories.
- [x] 4.2 Implement tag add/remove/list persistence through the note metadata writer.
- [x] 4.3 Implement note listing queries for folder selection, tag filtering, and stable sort order.
- [x] 4.4 Implement local search using SQLite index/FTS where available, without synchronous full-library scans on the UI thread.
- [x] 4.5 Add tests for folder tree display data, tag filtering, note listing, offline search, and index refresh after edits.

## 5. Editor, Viewer, And Split Workflow

- [x] 5.1 Implement raw Markdown editor buffer, cursor movement, text input, save, undo/redo baseline, and dirty-state tracking.
- [x] 5.2 Implement viewer layout/rendering for supported Markdown blocks and inline styles using SDL-native drawing.
- [x] 5.3 Implement editor-only, viewer-only, and split-pane layout modes for the active note.
- [x] 5.4 Implement debounced viewer refresh from editor buffer changes without blocking text input.
- [x] 5.5 Add tests or harness coverage for mode switching, dirty save behavior, split refresh, and unsupported extension rendering as ordinary Markdown/code.

## 6. Attachments

- [x] 6.1 Implement attaching a local file by copying it into a library-managed attachment directory for the owning note.
- [x] 6.2 Generate relative Markdown links for managed attachments and preserve attachment references through note rename/move operations.
- [x] 6.3 Implement inline image rendering for supported local image attachments with size constrained to the viewer pane.
- [x] 6.4 Render non-image attachments as clickable links and open them through the Linux default handler on explicit user activation.
- [x] 6.5 Reject managed rendering/opening for attachment paths that resolve outside the library root.
- [x] 6.6 Add attachment tests for copy/link creation, inline image model generation, non-image link activation command construction, and path traversal rejection.

## 7. Application Shell

- [x] 7.1 Build the single-window shell with folder/tag sidebar, note list, editor/viewer region, status bar, and basic command prompts.
- [x] 7.2 Implement library open/create flow using a local path with no import options.
- [x] 7.3 Connect folder selection, tag selection, note selection, search query, and editor/viewer state to the persistence services.
- [x] 7.4 Persist local UI state for last library, selected note, pane mode, sidebar widths, and sort order.
- [x] 7.5 Add smoke/integration tests for creating a library, creating a note, tagging it, searching it, switching split mode, and reopening the app state.

## 8. Performance And Offline Validation

- [x] 8.1 Add startup, note load, typing, viewer refresh, search, and large-folder scan timing counters or benchmark targets.
- [x] 8.2 Add a no-network build validation path that fails if configure/build attempts dependency downloads.
- [x] 8.3 Add regression fixtures for a medium library with folders, tags, Markdown content, and attachments.
- [x] 8.4 Document current limitations, supported Markdown subset, dependency requirements, and library format recovery behavior.
