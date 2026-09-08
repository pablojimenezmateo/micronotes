# Library Format And Limits

## Library Layout

A micronotes library is a local folder. Markdown files are the source of truth for note bodies.

```text
library/
  note.md
  folder/another-note.md
  .micronotes/
    index.sqlite
    attachments/<note-id>/<file>
    ui.state
    tree.state
    folds.state
    recovery/<note-id>.body
    trash/files/<name>
    trash/index
```

Everything under `.micronotes/` is view state or a rebuildable cache, except
`trash/` and `recovery/`. `trash/` holds deleted notes and notebooks until they
are restored or removed by hand; `recovery/` holds the buffer being edited,
rewritten on every keystroke, so that a crash between two characters does not
lose the second one. A recovery file is retired the moment the note it belongs
to is saved, so one being present at startup means the last session did not end
cleanly -- and micronotes offers it back rather than the older text on disk.

`ui.state` carries the appearance settings - theme, text size, page width -
along with the pane widths, the selection, the favorites and the recents, which
sidebar bands are shut, and what colour each tag is; `tree.state` which
notebooks the sidebar has open; `folds.state` which sections each note has
collapsed. None of it is ever written into a note.

`ui.state` is `key=value`, one per line, and unknown keys are ignored, so a file
written by a newer version still opens. `text_size` is `small`, `medium` or
`large` and `page_width` is `narrow`, `medium` or `wide`; anything else reads as
`medium`, because a hand-edited state file must not be able to leave the app in
a size nobody can read. Because these live beside the library, two libraries can
be typeset differently - which is the point on a machine where one of them is
read on an external monitor.

`collapsed=<band>` names a sidebar band that is shut - `notebooks`,
`favorites`, `tags` or `recent` - and only shut bands are written, so the usual
state costs nothing and a file from before bands existed reads as all four
open.

`tag_color=<swatch>|<tag>` is a tag's colour, and the swatch **index** comes
first because the tag name is the only field that could contain the separator.
It is an index into a fixed palette rather than an RGB: the light and dark
themes are different colours, so storing the pixels would mean a colour that
was legible on the theme it was picked in and possibly invisible on the other.
Only tags somebody actually chose a colour for are written - the rest take one
derived from the name, which is why a library's dots mean something before
anybody has opened a picker. An index the palette has no swatch for wraps, and
a line whose index will not parse is dropped rather than defaulted, so a
hand-edited file cannot silently repaint a tag.

Neither is part of the library. What colour somebody finds `work` easiest to
spot, and whether they keep the tags band shut because their library has sixty
of them, are facts about a reader rather than about a note - so writing either
into front matter would make a preference a library-wide edit, and would put it
in everyone's history.

The SQLite database is a rebuildable index/cache. If it is deleted, micronotes rebuilds it from Markdown files and metadata.

## Changes Made Outside micronotes

The Markdown files are the source of truth, which means anything else may write
them: another editor, a `git checkout`, a sync daemon. micronotes watches the
library tree and notices within a frame, without needing the window to be
touched.

What happens next depends on whether the note on screen has unsaved work in it:

- **Nothing unsaved.** The note is reloaded from the file, and the sidebar, the
  search index and the backlinks follow. The reader's place in the note is kept
  rather than reset to the top.
- **Unsaved work.** The buffer is left exactly as it is -- an external change
  never overwrites something you have typed. The next save then keeps *both*
  versions: the buffer is written to the note, and the text that had appeared on
  disk becomes a note of its own beside it, called
  `<name> (external change <timestamp>).md`. It is an ordinary note, with its
  own id, listed in the sidebar and searchable, so the two can be read side by
  side and merged by hand. The status line names the file it was kept as.
- **The file was deleted.** The buffer is the only copy left, so it is kept and
  the next save writes the file back.

A note whose front matter `id` changes on disk is followed rather than lost: it
is the same file, so the selection, its tab and the favorites re-point at the
new id.

The one thing micronotes never does is choose for you between two versions of a
note. If a change cannot be merged automatically, both copies end up in the
library.

## Note Metadata

Notes use a small front matter header:

```markdown
---
id: stable-note-id
title: Note title
icon: bookmark
tags: work fast local
---
```

The `id` is stable across rename and folder moves. `icon` names one of the marks
the shell draws - `star`, `check`, `flag`, `bookmark`, `tag`, `folder`,
`calendar`, `clock`, `bolt`, `warning`, `code` - shown beside the note in the
sidebar, the note list and the breadcrumb; the key is omitted entirely when a
note has no icon, and a name this version does not know is kept in the file and
drawn as the default page mark.

Tags are space-separated in the initial format. YAML's other two forms are read
as well - `tags: [work, fast]` and a `- item` block - and each is written back
in the form it was read in.

Front matter keys micronotes does not model are preserved exactly as written,
in their original order, including values that span several indented lines. A
note written by another tool can be opened, edited and saved here without
losing what that tool stored.

## The Note's Own Name

micronotes draws a note's name above its first block, from the front matter's
`title` or from the file's stem. A note that *also* opens with a `# <name>`
heading repeating it - which is what most other editors write - therefore said
its name twice, once as the page's title and once as its first line.

That heading is read as part of the note's header rather than as body text: it
is split off with the front matter on load and written back with it on save, so
the body being edited holds the name once and the file on disk still carries the
heading every other tool expects there. Renaming the note rewrites the heading
along with `title`, instead of leaving the file naming the note by a title it no
longer has.

The rule is deliberately literal. Only an ATX `# ` at the first level whose text
is the note's name exactly - once the surrounding spaces are off - is claimed. A
level-2 heading, a closed `# Name #`, a setext underline, and a heading that
differs by a word or by case are all body text and stay where the reader put
them.

Notes micronotes creates carry no such heading and never grow one: the name
lives in the library, and writing it into the Markdown as well is the
duplication this exists to undo.

## Markdown Scope

Supported scope is intentionally small:

- headings
- paragraphs
- emphasis and strong text as exposed by `md4c`
- lists
- blockquotes
- fenced code blocks
- links
- images
- tables where supported by `md4c`

Unsupported extensions, including Mermaid, math engines, remote embeds, scripts, plugins, and webview-only features, are displayed as ordinary Markdown/code where possible.

## Attachments

Attachments are copied into `.micronotes/attachments/<note-id>/`. Image attachments render inline when supported by the image backend. Non-image attachments render as links and open through `xdg-open` only after explicit user activation.

Managed attachment paths must resolve inside the library root.

## Durability

Every write micronotes makes to a library is atomic and durable: the bytes go to
a temp file beside the target, are `fsync`ed, and the temp is renamed over the
target, whose directory is then `fsync`ed as well. A crash leaves either the
previous file or the whole new one, never a half-written one.

The replaced file's properties survive the swap, which a plain rename would
destroy along with its inode:

- **A symlinked note stays a symlink.** The link is followed and its target is
  written, including a link whose target does not exist yet.
- **Its mode and owner are carried over.** A note kept at `0600` stays `0600`
  rather than coming back at whatever the umask says. The staging file is
  `0600` while it is being written, so a half-written note is never readable by
  anyone the finished one would not be.

Staging files are named `.<name>.microcore-write.<pid>.<n>`: hidden, distinctive
enough to recognise as debris a year later, and unique per process and per call,
so two writers of one file degrade to last-writer-wins rather than truncating
each other. The library walk skips them by name.

Deleting a note writes its trash-index entry *before* moving the file. The index
is the only record of where a deleted note came from, so the failure mode has to
be an index line naming a file that never arrived -- which reads as history and
is skipped -- rather than a file in the trash that nothing names and nobody can
restore.

## Performance Notes

Search uses the SQLite index/FTS path when available. Full library scans are
reserved for explicit refresh/rebuild paths, not for every search query -- and
not for saving, which re-indexes only the file it wrote.
