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
```

The SQLite database is a rebuildable index/cache. If it is deleted, micronotes rebuilds it from Markdown files and metadata.

## Note Metadata

Notes use a small front matter header:

```markdown
---
id: stable-note-id
title: Note title
tags: work fast local
---
```

The `id` is stable across rename and folder moves. Tags are space-separated in the initial format.

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
