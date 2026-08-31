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
    trash/files/<name>
    trash/index
```

Everything under `.micronotes/` is view state or a rebuildable cache, except
`trash/`, which holds deleted notes and notebooks until they are restored or
removed by hand. `ui.state` carries the appearance settings - theme, text size,
page width - along with the pane widths, the selection, the favorites and the
recents; `tree.state` which notebooks the sidebar has open; `folds.state` which
sections each note has collapsed. None of it is ever written into a note.

`ui.state` is `key=value`, one per line, and unknown keys are ignored, so a file
written by a newer version still opens. `text_size` is `small`, `medium` or
`large` and `page_width` is `narrow`, `medium` or `wide`; anything else reads as
`medium`, because a hand-edited state file must not be able to leave the app in
a size nobody can read. Because these live beside the library, two libraries can
be typeset differently - which is the point on a machine where one of them is
read on an external monitor.

The SQLite database is a rebuildable index/cache. If it is deleted, micronotes rebuilds it from Markdown files and metadata.

## Note Metadata

Notes use a small front matter header:

```markdown
---
id: stable-note-id
title: Note title
icon: 📓
tags: work fast local
---
```

The `id` is stable across rename and folder moves. `icon` is one emoji, shown
beside the note in the sidebar, the note list and the breadcrumb; the key is
omitted entirely when a note has no icon.

Tags are space-separated in the initial format. YAML's other two forms are read
as well - `tags: [work, fast]` and a `- item` block - and each is written back
in the form it was read in.

Front matter keys micronotes does not model are preserved exactly as written,
in their original order, including values that span several indented lines. A
note written by another tool can be opened, edited and saved here without
losing what that tool stored.

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

## Performance Notes

Search uses the SQLite index/FTS path when available. Full library scans are reserved for explicit refresh/rebuild paths, not for every search query.
