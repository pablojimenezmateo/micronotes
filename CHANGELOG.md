# Changelog

All notable changes to micronotes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/spec/v2.0.0.html) with the usual
pre-1.0 caveat: while the major version is 0, a minor bump may break the
library format or the persisted UI state, and the entry will say so.

The newest entry always states the version in `CMakeLists.txt` --
`tools/check-doc-versions.sh` enforces it, and `tools/release.sh` refuses to
publish when it does not.

## [0.6.0] - 2026-09-14

The first published release of micronotes, and the first one with a release
lifecycle behind it: the package is built from a clean Release tree, tested,
proven to start on a machine that is not the maintainer's, checksummed and
GPG-signed before it is tagged.

Notes are plain `.md` files in one folder you choose. micronotes reads and
writes those files and nothing else -- no database of record, no sync, no
network. Delete the app and the notes are still there.

### The editor

- You type into formatted content rather than into source. Headings, emphasis,
  code spans, links and task checkboxes are drawn where you type them, and a
  block's syntax markers appear only while the caret is inside it.
- Raw Markdown (`Ctrl+2`), a reading view (`Ctrl+3`) and a split (`Ctrl+4`).
- Blocks with hover handles: drag to reorder, `/` to insert, `Esc` to select,
  and a menu for turn-into, duplicate, delete and move.
- UTF-8 caret motion, keyboard selection, word motion and real undo.
- Every note is headed by its own name, editable where it is drawn, and by the
  front matter it carries, shown as properties. A note that arrived from another
  editor with a `# Title` first line keeps it, and that line follows a rename
  rather than being printed twice.

### Finding things

- One sidebar with the search field on top of it, in bands that can be shut --
  notebooks, pinned, tags, recents -- each carrying a count.
- `Ctrl+F` finds in the note, with a match count, match-case and whole-word
  toggles, and every hit highlighted in whichever panes are showing.
  `Ctrl+Shift+F` searches every note, and opening a result carries the query
  into the note.
- `Ctrl+P` / `Ctrl+O` jumps to any note, `Ctrl+Shift+P` is every command, `F1`
  is every shortcut.
- Wikilinks between notes, completed as you type, with a panel of what links
  back.
- Tags are a filter across the tree rather than a second hierarchy, each with a
  colour stored as a palette choice rather than an RGB, so one pick reads
  correctly in both themes.

### The window

- A borderless window with drawn chrome: a menu bar carrying the window
  buttons, a tab strip, a breadcrumb, and a status bar of segments that say what
  they mean when hovered. If the platform will not take a hit test, the
  decorations come back.
- Chrome in one monospaced face at one size, and the note itself in a
  proportional one with a heading scale.
- More than one note open at a time, with a tab strip that reorders and scrolls.
- Light and dark, HiDPI, and a palette held to a contrast ratio rather than
  hoped at.

### Notes as files

- Attachments land beside the note; a notebook can keep its files with it.
- A change made in another program is noticed when it happens, and if both
  versions cannot be reconciled **both are kept** -- the version found on disk
  is filed beside the note as a note of its own rather than either being
  dropped.
- Deleting moves to the library's own trash, so it can be undone.
- Export a note as PDF.
- Session, tabs and pane arrangement come back on restart.

### Speed

Re-layout after a keystroke in a 200 KB note is budgeted and measured, not
hoped for, by a Release perf harness that fails when a scenario goes over
budget. Peak resident memory over the harness run is about 31 MB.

### Known limitations

- Linux x86_64 only.
- The `.deb` bundles the SDL3 libraries, because no Debian-family distro
  packages SDL3 yet.
- SDL3_image is optional; without it, images in notes are not decoded and
  reserve no space.
- Pre-1.0: while the major version is 0, a minor bump may change the library
  format or the persisted UI state, and the changelog will say so.

## [0.5.0] - Unreleased

The state the project reached before it had a release lifecycle. **Never
tagged and never published**: no `v0.5.0` exists, and this entry is here so the
history has a floor rather than to describe a release. Everything up to this
point is in the git log; the first published release is the entry above this
one once it is cut.
