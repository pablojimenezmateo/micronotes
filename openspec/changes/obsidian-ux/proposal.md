## Why

The shell was drawn to Notion's model, and it still reads that way: a blue
accent, a sidebar lighter than the page it sits beside, callout kinds badged in
the far corner, code blocks fenced by a rule rather than sat on a surface, and a
status bar whose whole width is a list of keys that `F1` already holds.

Notion and Obsidian disagree about what a notes app is, and micronotes has since
taken Obsidian's side of every argument that matters: the notes are files in a
folder, the links are `[[wikilinks]]`, the panel on the right is backlinks, and
nothing goes over the network. The presentation is the last part still arguing
for the other one.

This change moves the interface to Obsidian's, which is a different claim from
"restyle it": Obsidian's surface hierarchy is inverted from Notion's, its
chrome starts with a ribbon rather than with a breadcrumb, and it puts two
things on the page -- the note's own name, and its front matter -- that
micronotes currently keeps off it.

## What Changes

- **Palette.** Purple accent, and Obsidian's surface order: the chrome sits
  *away* from the page in both modes -- darker than it in dark, greyer than it
  in light. Light mode barely moves; dark mode inverts, because today's sidebar
  is the lighter of the two.
- **A rounded-rectangle primitive**, because almost every Obsidian surface has a
  radius and the draw layer currently cannot express one.
- **Callouts** get a title row at the top left -- icon, kind, bold, in the
  callout's colour -- instead of a badge in the top right corner that the first
  line of text can crowd out.
- **Code blocks** sit on a rounded surface with the language and copy control at
  the top right, rather than being fenced above by a rule and labelled below.
- **Task checkboxes** take Obsidian's shape, and a completed task's text goes
  muted and struck through.
- **An activity ribbon** down the far left: new note, quick switcher, search,
  and settings, with the panel toggles at the foot.
- **An inline title** at the top of every note: the file's own name, editable in
  place, which is where Obsidian renames a note from.
- **Front matter as properties**: the keys a note carries are shown at the top of
  the page instead of being parsed and hidden.
- **The sidebar** gains indent guides and Obsidian's row density.
- **The status bar** stops being a key legend and becomes what Obsidian's is:
  word and character counts, right-aligned, with transient status to their left.
- **Shortcut aliases**: `Ctrl+O` opens the quick switcher and `Ctrl+E` toggles
  editing and reading, alongside the existing bindings rather than replacing
  them.

## Non-Goals

- No graph view, no canvas, no plugins, and no sync. `product-vision` excludes
  all four and this change does not reopen any of them.
- No second sidebar-tab hierarchy. The unified sidebar with search on top of the
  tree stays as it is; the ribbon sits beside it rather than replacing its
  search field with a tab.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `markdown-workflow`: adds an inline note title and a properties display for
  front matter. Both are presentation of data the library already holds.
- `note-organization`: adds the activity ribbon, and renaming from the page.
- `product-vision`: the presentation requirement names the interface's own
  measurable floor -- contrast -- rather than only theme and scale.

## Impact

- `src/ui/Draw.{h,cpp}`, `src/ui/Theme.cpp`, `src/ui/Metrics.h`: the primitive,
  the palette, and the radius and ribbon tokens.
- `src/ui/ShellLayout.{h,cpp}`: a ribbon column left of the sidebar.
- New `src/app/Ribbon.{h,cpp}` and `src/app/Properties.{h,cpp}`, so neither
  lands in `Application.cpp`.
- `src/app/Chrome.cpp` (status bar), `src/app/PageView.cpp` (callouts, code,
  checkboxes), `src/app/TabStrip.cpp`, `src/doc/Layout.cpp` (callout title band).
- No change to the library format, the `.md` files, the SQLite index, or the
  md4c render model. Nothing here touches the network, and nothing here is
  stored in a note that another Markdown tool would not already read.
