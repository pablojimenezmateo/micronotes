## Context

micronotes starts from an empty repository and targets the same engineering posture as microide: native C++/SDL3, Linux-first, single-window, keyboard-friendly, local-only, and fast enough to validate with internal baselines. The product borrows Joplin's core note-taking model, but deliberately excludes Joplin's cross-platform clients, sync, cloud accounts, plugins, web clipper, WYSIWYG editor, importers, and renderer extensions.

The application owns a local notes library. Markdown files and attachment files are the durable source of truth. SQLite is used as a rebuildable cache/index for metadata, note discovery, tag lookup, and search speed, not as the only copy of user-authored note bodies.

## Goals / Non-Goals

**Goals:**

- Build a Linux desktop notes app with C++20, CMake, SDL3, SQLite, and vendored `md4c`.
- Keep Markdown files readable and editable outside micronotes.
- Provide folders/notebooks, tags, note list, search, raw Markdown editor, rendered viewer, and split-pane mode.
- Render a small Markdown feature set with deterministic SDL-native drawing and inline image attachment support.
- Store attachments inside the library and open non-image files with the system default handler.
- Keep dependency and runtime behavior compatible with offline use.

**Non-Goals:**

- No sync, networking, cloud accounts, telemetry, remote plugin installation, or online help.
- No importers from Joplin, Evernote, Obsidian, or any other app.
- No WYSIWYG editor, Mermaid, math rendering, diagrams, custom Markdown plugins, or embedded browser/webview.
- No Windows, macOS, mobile, or web support.
- No plugin runtime in the initial product.

## Decisions

### Use File-Authoritative Storage With SQLite Index

Notes live as `.md` files below the selected library root. SQLite stores derived metadata: note id, path, title, folder id/path, timestamps, tags, attachment references, and searchable text tokens or FTS rows. The index is rebuildable from files and metadata sidecars.

Alternatives considered:

- SQLite-authoritative note bodies: simpler rename/link tracking but makes the library less transparent and harder to recover manually.
- Plain files only: lowest dependency count but weaker search and slower startup for larger libraries.

### Use Stable Note IDs Stored In Metadata

Each note gets a stable id stored in lightweight front matter or an adjacent metadata sidecar. The file path remains user-readable and may change when a folder/note is renamed; the id preserves attachment references and cached index identity.

Alternatives considered:

- Path-only identity: simple, but renames break cached references and attachment ownership.
- Hidden database-only identity: fast, but fragile if the SQLite file is deleted.

### Vendor `md4c` And Render Natively

`third_party/md4c` is checked into the repository and built through CMake. micronotes parses Markdown with `md4c`, maps the parsed blocks/inlines into an internal render model, and draws text, code blocks, lists, links, and images with SDL-rendered surfaces.

Alternatives considered:

- Webview/HTML renderer: high Markdown compatibility but violates the minimal native dependency posture.
- Hand-written parser: fewer dependencies but high correctness risk and unnecessary effort.
- Full CommonMark extension stack: more feature-rich but outside the intentionally small product.

### Keep UI Host-Owned And Single-Window

The shell owns folders/tags sidebar, note list, editor pane, viewer pane, status bar, command prompts, and dialogs. Modes are editor-only, viewer-only, and split. Pane layout and selection state are persisted locally.

Alternatives considered:

- Plugin-contributed panes: flexible but creates security and scope problems.
- Multi-window support: useful for some workflows but expands platform and state complexity.

### Store Attachments In Library-Local Directories

Attachments are copied into a library-managed attachment directory associated with the owning note id. Markdown links use relative library-safe paths. Image attachments render inline when the format is supported; other attachments render as clickable links and launch with `xdg-open`.

Alternatives considered:

- Link to arbitrary external files: avoids copying but breaks portability and risks leaking local filesystem assumptions into notes.
- Store attachments as database blobs: simple ownership but opaque and heavier to recover.

### Prefer Linux System Packages Except Vendored Markdown Parser

SDL3, SQLite, and optional SDL image/font helper libraries are expected as Linux development packages discovered by CMake/pkg-config. `md4c` is vendored to keep Markdown parsing available without network access during normal development.

Alternatives considered:

- Vendor every dependency: more reproducible but increases repository size and maintenance.
- Fetch dependencies at configure time: convenient but violates offline development.

## Risks / Trade-offs

- [Risk] SQLite cache diverges from files after external edits or crashes -> Rebuild indexes on demand, track file mtimes/sizes, and provide a startup consistency scan.
- [Risk] File-authoritative notes make tag storage ambiguous -> Use explicit metadata front matter or sidecar files and define one canonical writer.
- [Risk] Native Markdown rendering misses edge cases users expect from browser renderers -> Keep the supported subset explicit and test parser/render model behavior with fixtures.
- [Risk] `xdg-open` can launch arbitrary local handlers -> Treat attachment opening as an explicit user action and never auto-open files from Markdown rendering.
- [Risk] Inline image loading can block UI on large files -> Load/decode images asynchronously and cache scaled surfaces by path, mtime, and display width.
- [Risk] Linux-only scope still faces distro dependency variation -> Support both CMake package and pkg-config discovery for SDL3/SQLite-related dependencies.

## Migration Plan

This is the initial product bootstrap, so there is no existing user data to migrate. Implementation should create new libraries only after the user selects or accepts a local root. If the SQLite schema changes before release, the app can delete and rebuild the cache because Markdown files and metadata remain authoritative.

## Open Questions

- Exact metadata format: Markdown front matter versus sidecar files. Default recommendation is minimal YAML-like front matter for tags/title/id, with sidecars reserved only if front matter proves too invasive.
- Image dependency: use `SDL3_image` if available, or implement only a tiny built-in image path initially. Default recommendation is `SDL3_image` as an optional-but-enabled dependency because inline images are core scope.
