## Context

The library walk is one `recursive_directory_iterator` over the root, pruning
the state directory and keeping `.md` files. The index reports the directories
that walk passed so the tree can draw empty notebooks without a second walk.
The sidebar's row list is a pure function of the catalog revision and what the
tree has open; every disk command goes through `NoteCatalog`, which re-indexes
what it wrote and drops exactly the memos it invalidated.

Companion files have to fit that shape: one walk, one revision, one door.

## Decisions

### The name is the rule, spelled once

`kFilesDirName = "files"` lives in `library/Library.h` with three questions
beside it -- `isFilesDir`, `insideFilesDir`, `filesRootOf` -- and nothing else
compares a path component to the string. The match is exact and case-sensitive.
A name a reader could plausibly already have used for a notebook was accepted
over `_files` because the tree reads better, and the refusal to create or
rename a notebook to it is what keeps the collision from being silent.

*Alternatives:* a hidden `.files` -- rejected, it would be invisible in a file
manager, which is where the reader organises them; a per-library configurable
name -- rejected, a library has no configuration file and this is not the
feature to introduce one for.

### Companions ride the existing walk, classified by depth

`Library::walk` gains a third output. On meeting a directory named `files`
outside a files area it records the iterator's depth; every entry deeper than
that is a companion until the depth comes back. O(1) per entry, no component
scans, and nothing under a files directory reaches the note or directory lists
-- so a note inside one cannot be a note the index knows and the tree cannot
place. The index carries the list beside its directories; the organisation
service memoises it beside the folders; nothing about a companion enters SQLite.

### The watcher re-walks one files directory, not the library

A path under a files directory routes to `NoteCatalog::refreshFilesDir`, which
walks `<notebook>/files` alone and swaps that subtree into the memo. The two
counters -- `library.companion_entries_visited` and
`library.files_dir_refreshes` -- exist so that this staying narrow is
something the harness can see rather than believe.

### Two tree row kinds, one launcher seam

`TreeRowKind::FilesFolder` and `TreeRowKind::File`, with the entry's own path in
a new `TreeRow::file` and its parent in `folder` -- the same shape a note row
has. A `File` opens through `ui.launcher`, defaulting to `openWithDesktop`, so
the rule that a `Click` launches and a `Cursor` never does is a unit test rather
than a screenshot. Neither kind is ever the selection: there is no page for a
file and a files directory is not a notebook.

### Search results reuse the tree's file row

Name matches are appended under a "N files" caption as ordinary `File` tree rows
at depth 0, so activation, the right-click menu and drags work on a result with
no second code path. `SidebarSearch` bundles the note results and the file
matches under the one `SearchKey` memo because they are asked together and go
stale together.

### Management goes through the catalog, like everything else

`renameCompanion`, `moveCompanion`, `deleteCompanion` and `createCompanionFolder`
on `NoteCatalog`, each ending in `refreshFilesDir` for the roots it touched.
`Library::moveCompanion` is the one primitive under rename and move; it refuses
the anchor at either end, a move out of a files area, and a directory into its
own descendant. Delete reuses the trash index and `restoreFromTrash` unchanged;
`uniquePath` already preserves an extension.

The companion a menu is about travels in `ui.sidebar.companionTarget`, set by
the right click, because notes carry that in `selection.noteId` and notebooks in
`selection.folder` and a companion has neither.

## Risks

- A library that already has a notebook named `files` sees its notes become
  companions. The refusal to *create* one does not undo one that exists;
  `docs/library-format.md` says so.
- A files area with many subfolders spends inotify watches. The watcher already
  falls back to rescan-only past its budget.
