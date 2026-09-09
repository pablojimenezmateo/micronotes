## Why

A micronotes library is Markdown only. `Library::walk` drops every entry that
is not a `.md`, the watcher ignores them, and the one place a non-note file can
live -- `.micronotes/attachments/<note-id>/` -- is per note and hidden. There is
no way to keep the PDF, the diagram and the recording that belong *with* a
notebook in that notebook, where the app can list them and the reader can find
them by name.

The premise of the app is that the library is an ordinary folder. Ordinary
folders hold files.

## What Changes

**A `files/` directory inside any notebook holds companion files.** Everything
under it, recursively, is a file rather than a note: listed in the sidebar tree
under its notebook, found by name, opened through the desktop's default handler,
and never read, rendered or indexed by micronotes. A `.md` inside one is a file.

**The tree, the search and the sidebar's menus learn the two new row kinds.**
A files directory unfolds like a notebook but cannot be selected; a file opens
externally on a click or Enter and never as the cursor passes over it. Search
lists name matches under their own caption. Files and the folders inside a
files area can be renamed, moved -- by drag or by the "Move to notebook"
palette -- and deleted to the library trash, from where they can be restored.

**The rule is enforced at the library.** A notebook cannot be created or
renamed to `files`, a note cannot be created in or moved into a files area, and
a notebook cannot be moved into one. The `files` directory itself can be
deleted but not renamed or moved.

**Changes under `files/` from outside cost one small walk.** The watcher routes
a path under a files directory to a re-walk of that directory alone, not to the
library refresh a folder operation costs.

## Non-Goals

- Rendering. No thumbnails, no previews, no inline images from `files/`. The
  desktop opens them; micronotes files them.
- A file type table. One mark for every kind of file.
- Indexing file contents. Search is by name and stays by name.

## Impact

- `note-organization` gains a **Companion Files** requirement.
- `docs/library-format.md` documents the layout and the rule.
- `src/library/` owns the rule (`kFilesDirName`, `isFilesDir`,
  `insideFilesDir`, `filesRootOf`); `src/ui/TreeModel` and the sidebar draw
  and answer for the two new row kinds; two perf counters cover the walk and the
  narrow refresh.
