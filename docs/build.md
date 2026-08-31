# Build Instructions

micronotes is Linux-only. Normal builds must work offline after dependencies are installed: CMake must not fetch project dependencies during configure or build.

## Ubuntu 24.04 / Debian Setup

Install the base toolchain and development packages:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  git \
  pkg-config \
  cmake \
  ninja-build \
  python3 \
  curl \
  ca-certificates \
  libsqlite3-dev \
  libx11-dev \
  libxext-dev \
  libxrandr-dev \
  libxcursor-dev \
  libxi-dev \
  libxinerama-dev \
  libxkbcommon-dev \
  libwayland-dev \
  wayland-protocols \
  libegl1-mesa-dev \
  libgl1-mesa-dev \
  libdbus-1-dev \
  libibus-1.0-dev \
  libudev-dev \
  libasound2-dev \
  libpulse-dev \
  libpipewire-0.3-dev \
  libdecor-0-dev \
  libfreetype-dev \
  libharfbuzz-dev
```

`libfreetype-dev` and `libharfbuzz-dev` are needed when building `SDL3_ttf`. Without them, SDL3_ttf configure can fail with missing `FREETYPE_LIBRARY`, `FREETYPE_INCLUDE_DIRS`, or harfbuzz detection errors.

## SDL3 From Source

Install SDL3 into `/usr/local`:

```bash
mkdir -p ~/src
cd ~/src

git clone https://github.com/libsdl-org/SDL.git SDL3
cd SDL3
git checkout release-3.2.16

cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DSDL_TEST_LIBRARY=OFF

cmake --build build
sudo cmake --install build
sudo ldconfig
```

Install SDL3_image:

```bash
cd ~/src

git clone https://github.com/libsdl-org/SDL_image.git SDL3_image
cd SDL3_image
git checkout release-3.2.4

cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build build
sudo cmake --install build
sudo ldconfig
```

Install SDL3_ttf:

```bash
cd ~/src

git clone https://github.com/libsdl-org/SDL_ttf.git SDL3_ttf
cd SDL3_ttf
git checkout release-3.2.2

cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build build
sudo cmake --install build
sudo ldconfig
```

If SDL3_ttf was already configured before installing `libfreetype-dev` or `libharfbuzz-dev`, remove its build directory and configure again:

```bash
cd ~/src/SDL3_ttf
rm -rf build
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
```

## Verify Dependencies

```bash
cmake --version
git --version
pkg-config --modversion sdl3 sqlite3
pkg-config --modversion SDL3_image SDL3_ttf
```

## Build micronotes

From the repository root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the performance harness:

```bash
./build/bin/micronotes_perf
```

Run the app with an explicit local library:

```bash
./build/bin/micronotes --library ~/Notes/micronotes
```

## Install a Desktop Launcher

Install into a user prefix so the app appears in the application menu and can be pinned from there:

```bash
cmake --install build --prefix ~/.local
```

This installs:

- `micronotes` into `~/.local/bin`
- a `.desktop` launcher into `~/.local/share/applications`
- the app icon into `~/.local/share/icons/hicolor/scalable/apps`

Set the default notes path once and the launcher will reuse it on later starts:

```bash
./build/bin/micronotes --set-library ~/Notes/micronotes
```

The chosen path is stored in `~/.config/micronotes/library-path`. You can still override it for a single launch with `--library <path>`.

## Build an Installer Package

Create a Debian package from the build tree:

```bash
cmake --build build --target package
```

The package is written to the build directory and includes the desktop launcher and icon.

Useful runtime controls:

- Use the visible pane toolbar buttons for refresh, new, rename, delete, save, tags, and pane mode changes.
- `Ctrl+N`: create a note in the selected folder.
- `Ctrl+S`: save the current note and refresh search.
- `Ctrl+R`: refresh the local library/index after external edits.
- `Ctrl+T`: edit space-separated tags for the selected note.
- `Ctrl+V`: paste text, or paste image data when the clipboard has no text.
- `Ctrl+Shift+V`: paste clipboard image data as a managed image attachment, falling back to text.
- `Ctrl+Z`, `Ctrl+Y`: undo and redo editor changes. A run of typing undoes as
  one step; a structural edit or a pause of about 600 ms starts a new one.
- `Ctrl+B`, `Ctrl+I`, `Ctrl+E`: bold, italic, inline code around the selection.
  Pressing the same key again removes the markers.
- `Ctrl+K`: turn the selection into a link and put the caret in the empty `()`.
  Outside the editor there is no selection to link, so it opens the note jump
  instead.
- `Ctrl+P`: jump to any note in the library by fuzzy title.
  `Ctrl+Shift+P`: the command palette - every shell command in one filterable
  list, including the ones with no shortcut: set a note icon, move a note or a
  block selection to another note, restore from trash, toggle a favorite,
  settings.
- `F1`: every keyboard shortcut in one list, filterable by what it does or by
  the keys themselves - typing `alt` finds `Alt+Up`. The status bar names three
  ways in and then points here rather than listing a dozen keys it has no room
  for.
- `Ctrl+,`: settings. Theme, text size, page width, and the library folder. Each
  row opens the list of its own values and the settings list comes back with the
  new one on it, so changing two things does not mean opening the dialog twice.
  Text size scales the whole type scale together, so headings keep their
  proportion to body text; page width caps the content column, and the extra
  room becomes margin rather than longer lines. Both are stored per library in
  `.micronotes/ui.state`, beside the theme.
- Choosing a library folder from settings opens it without a restart: the
  library being left is written out first, so its open note, favorites and folds
  stay with it. The folder is created if it is not there, `~` is expanded, and
  the choice is remembered in `~/.config/micronotes/library-path` the same way
  `--set-library` does.
- `Enter`: continues the list, quote, or callout you are in. On an empty item it
  leaves the block, and leaves a blank line behind it - without one the next
  line would be a lazy continuation and the file would still say the text was
  part of the item. On the opening line of an unclosed fence it adds the closing
  fence.
- `Tab`, `Shift+Tab`: indent and outdent the list item under the caret. Outside
  a list, `Tab` still inserts two spaces.
- `Backspace` at a block's first character strips the block's marker, outdenting
  a nested list item first. `Ctrl+Backspace` and `Ctrl+Delete` work by word.
- `Ctrl+Enter`: tick or untick the task under the caret. Task checkboxes are
  also clickable in the live surface, except in the block holding the caret,
  where the raw `- [ ]` is shown instead.
- `Ctrl+D`, `Ctrl+Shift+D`: duplicate or delete the block under the caret.
- `Alt+Up`, `Alt+Down`: move the block past its neighbour, carrying the blank
  line that separated them so two paragraphs never run together.
- `Ctrl+Shift+0/1/2/3`: turn the block into text or a heading.
  `Ctrl+Shift+7/8/9`: numbered item, bullet, task.
- `/` at the start of a line or after a space opens the block inserter: a
  fuzzy-filtered list of block types. `Esc` closes it and leaves the `/` alone.
- `Esc` in the live surface selects the block the caret is in; a second `Esc`
  puts the caret back. With blocks selected, `Up`/`Down` walk them,
  `Shift+Up`/`Shift+Down` extend the range, `Shift+click` extends it to the
  block clicked, and duplicate, delete, move, turn-into and `Ctrl+C` all act
  over the whole range at once. `Enter` returns to editing the text.
- Hovering a block in the live surface shows two controls in the left margin:
  `+` inserts a new block below it (nothing is written until you pick a type),
  and the dotted handle drags the block - or the whole block selection - to the
  line marked by the drop indicator.
- Right-clicking a block opens its menu: turn into, duplicate, fold, move up or
  down, and delete.
- `Ctrl+.` folds the heading or list item the caret is in, hiding everything
  nested under it: a heading owns its section down to the next heading of the
  same rank, a list item owns the items indented beneath it. A disclosure
  triangle in the left margin does the same with the pointer, and stays visible
  while a block is collapsed. Folding is a view preference, so it never touches
  the file; which toggles a note has collapsed is stored in
  `.micronotes/folds.state` and keyed by the block's text, so it survives edits
  elsewhere in the note.
- Selecting text raises a formatting toolbar above it: bold, italic, code,
  strikethrough, link, and turn into.
- `Ctrl+Left`/`Ctrl+Right` move by word, `Ctrl+Home`/`Ctrl+End` jump to the ends
  of the note, `PageUp`/`PageDown` move by a screenful, and holding `Shift` with
  any movement extends the selection.
- Typing `[] `, `[ ] ` or `[x] ` at the start of a line writes a real task
  marker. Every other Markdown shortcut (`# `, `- `, `1. `, `> `, `---`,
  ` ``` `) is already the syntax it looks like, so it is left exactly as typed.
- A run of `>` lines is drawn as one quote or one callout rather than one per
  line. `> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]` and `[!CAUTION]`
  each get their own colour and a badge, in the live surface and the reading
  view alike; the slash menu and the turn-into menu offer all five.
- A fenced code block shows its language and a `Copy` button in its top right.
- A note can carry an `icon:` emoji in its front matter, shown beside it in the
  tree, the note list and the breadcrumb. Set it from the command palette; an
  empty value removes the key. Front matter keys micronotes does not model are
  preserved exactly as they were written, so a note written by another tool
  survives being saved here.
- `Ctrl+1`: live surface. Markdown is rendered where you type it: headings,
  emphasis, code spans, links, list bullets, and task checkboxes all show as
  formatting, and a block's syntax markers appear only while the caret is in it.
  Tables, raw HTML, footnote definitions, and indented code render read-only
  through the reading-view renderer; click one to edit it as raw Markdown until
  the caret leaves it. This is the default.
- `Ctrl+2`: raw Markdown source. The escape hatch for anything the live surface
  does not model.
- `Ctrl+3`: reading view. `Ctrl+4`: source beside the reading view.
- `Ctrl+L`: cycle through the four pane modes.
- `Ctrl+Shift+L`: switch between the light and dark theme. The choice is stored
  in the library's `.micronotes/ui.state`.
- `/`: focus search when the editor is not focused.
- Click in the editor to place the cursor.
- Right-click a note in the note list for Rename and Delete actions.
- The sidebar is a tree: notes nest under their notebook, a disclosure triangle
  opens a notebook without selecting it, and clicking a row selects it. `Up` and
  `Down` walk the rows, `Right` opens a notebook or steps into it, and `Left`
  closes it or goes back to its parent. Which notebooks are open is stored in
  the library's `.micronotes/tree.state`; it is a view preference and never
  touches a file.
- Drag a note onto a notebook to move it, or a notebook onto another to
  re-parent it. The row you would drop on is outlined. A notebook refuses to be
  dropped inside itself or inside one of its own children.
- Favorites sit above the tree and the notes you opened most recently below it;
  tags are a filter at the bottom rather than a second way to organise notes.
  Both lists are stored in `.micronotes/ui.state` and name notes by id.
- Above the page, a breadcrumb names the notebooks down to the open note; click
  one to go there. The star at the right pins the note to Favorites.
- Deleting a note or a notebook moves it to the library's own
  `.micronotes/trash/`, not the desktop trash, so "Restore from trash..." in the
  command palette can put it back - with its attachments, and under a new name
  if something has taken the old one.
- Drag a local file onto the editor to copy it into managed attachments and insert a Markdown link.
- `Esc`: close an open dialog, or clear search focus and return to the editor.
- Rename, tag editing, notebook naming, and deletion happen in dialogs. `Enter`
  confirms and `Esc` cancels; clicking outside dismisses. A list longer than the
  dialog scrolls: the arrows carry the highlight with them, the wheel scrolls
  it, and a thumb on the right says there is more. A dialog never grows past the
  bottom of the window, so a short window shows fewer rows rather than a list
  running off the screen.
- Every empty place - no library, a library with no notes, an empty notebook, a
  tag nothing carries any more, a search that matched nothing, a note with no
  text in it - says which of those it is and which key does something about it.

## Appearance

micronotes ships light and dark themes and renders with the fonts vendored in
`third_party/fonts` (Inter for the interface, JetBrains Mono for code), falling
back to installed system fonts if those files are missing. The window follows
the display scale reported by the compositor, so text stays sharp on HiDPI
screens.

Theme, text size and page width are set from `Ctrl+,` and stored in the
library's `.micronotes/ui.state`. Display scale and text size are different
things and both apply: the compositor says how big a pixel is, the text size
says how big you want the type on top of that.

Note icons are drawn through the installed emoji font, scaled down to the icon
box. Emoji typed into a note's *text* are a different matter: a colour emoji
font such as Noto Color Emoji carries a single fixed bitmap strike - 128 pixels
- and SDL3_ttf cannot resize it, so attaching it to the body face would paint
one emoji over the four lines around it. It is therefore attached only if it
honours the size asked for, which the monochrome Noto Emoji does and the colour
one does not; where neither is installed, a note with no icon still gets a drawn
page mark rather than a tofu box.

## Debug And Capture Flags

These exist to make UI work reproducible and are not part of normal use:

```bash
# Render one frame to a PNG and exit.
./build/bin/micronotes --library ~/Notes --screenshot /tmp/shot.png

--size 1400x900        # window size
--theme light|dark     # override the stored theme
--scale 2              # override the display scale
--pane live|editor|viewer|split
--select <title>       # open the first note whose title contains this
--open rename|tags|new-folder|note-menu|folder-menu|delete-note|settings|shortcuts|command-palette
```

Set `MICRONOTES_DEBUG_INPUT=1` to log key, clipboard, and font-loading
diagnostics to stderr. Set `MICRONOTES_FONT_DIR` to load fonts from elsewhere.

Attach a file to the note stored in the last UI state:

```bash
./build/bin/micronotes --headless --library ~/Notes/micronotes --attach /path/to/file.png
```

The build contains an offline invariant check by default. It fails configure if project CMake helper files contain dependency-fetch mechanisms such as CMake fetch helpers, external-project downloads, `git clone`, or URL downloads.

For a release build:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
