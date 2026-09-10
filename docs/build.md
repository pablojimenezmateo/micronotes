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

### Checked builds

`CMakePresets.json` carries the sanitizer configurations and the
warnings-as-errors debug build the tree is expected to compile clean under:

```bash
cmake --preset micronotes-debug && cmake --build build/micronotes-debug
ctest --preset micronotes-debug

cmake --preset micronotes-asan  && cmake --build build/micronotes-asan  && ctest --preset micronotes-asan
cmake --preset micronotes-ubsan && cmake --build build/micronotes-ubsan && ctest --preset micronotes-ubsan
cmake --preset micronotes-tsan  && cmake --build build/micronotes-tsan  && ctest --preset micronotes-tsan
```

`tools/run-checks.sh` drives the same ground and keeps the full output of each
lane in `${LOG_DIR:-/tmp}/micronotes-<lane>.log`, so a result can be read back
without rerunning it:

```bash
tools/run-checks.sh tests        # build + ctest
tools/run-checks.sh clang-build  # whole tree under clang, warnings as errors
tools/run-checks.sh all          # every lane in sequence
```

The `clang-build` lane is the second-compiler gate: every other lane uses the
default compiler, so without it a clang-only compile break reaches `main`
unseen. It compiles and links the default target and does not rerun the tests.

ThreadSanitizer dies at startup with `unexpected memory mapping` on kernels that
hand out more address-space randomness than it can model - check with
`cat /proc/sys/vm/mmap_rnd_bits`, which has to be 28 or lower. That is the
sanitizer's own limitation, not a finding. `run-checks.sh` works around it per
process with `setarch -R`; by hand, run the binary the same way:

```bash
setarch -R ./build/micronotes-tsan/bin/micronotes_tests
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
- `Ctrl+P`: jump to any note in the library by fuzzy title. A note is found by
  its folder as well, but never *above* one whose title matched: the two are
  ranked in that order rather than on one score, or a note in `projects/` would
  outrank the note actually called "Product roadmap".
  `Ctrl+Shift+P`: the command palette - every shell command in one filterable
  list, including the ones with no shortcut: set a note icon, move a note or a
  block selection to another note, restore from trash, toggle a favorite,
  settings.
- `Ctrl+F`: find in this note. A bar floats at the top right of the page with
  the query, two toggles - `Aa` for match case and `ab` for whole word, or
  `Alt+C` and `Alt+W` - and how many matches there are. Every match is
  highlighted in whichever panes are showing; the one you are on is picked out
  and selected, so closing the bar leaves it there to copy. `Enter` and
  `Shift+Enter` step through them from the field, `F3` and `Shift+F3` from
  anywhere, and both wrap. With text selected, `Ctrl+F` searches for it rather
  than making you type it again, and opening a note from a "Search all notes"
  result brings that query into the bar with it -- reaching a note through a
  search and then retyping what you searched for is the step the bar exists to
  remove. It stays shut for a result whose *body* does not hold the query,
  which a title match can produce. `Esc` puts the bar away.
- `F1`: every keyboard shortcut in one list, filterable by what it does or by
  the keys themselves - typing `alt` finds `Alt+Up`.
- `Ctrl+,`: settings. Theme, text size, page width, and the library folder. Each
  row opens the list of its own values and the settings list comes back with the
  new one on it, so changing two things does not mean opening the dialog twice.
  Text size scales the whole type scale together, so headings keep their
  proportion to body text; page width caps the content column, and the extra
  room becomes margin rather than longer lines. Both are stored per library in
  `.micronotes/ui.state`, beside the theme.
- Choosing a library folder from settings opens it without a restart: the
  library being left is written out first, so its open note and favorites stay
  with it. The folder is created if it is not there, `~` is expanded, and
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
  also clickable in the reading view.
- `Ctrl+D`, `Ctrl+Shift+D`: duplicate or delete the block under the caret.
- `Alt+Up`, `Alt+Down`: move the block past its neighbour, carrying the blank
  line that separated them so two paragraphs never run together.
- `Ctrl+Shift+0/1/2/3`: turn the block into text or a heading.
  `Ctrl+Shift+7/8/9`: numbered item, bullet, task.
- `/` at the start of a line or after a space opens the block inserter: a
  fuzzy-filtered list of block types. `Esc` closes it and leaves the `/` alone.
- A block command applies to every block the selection covers, so selecting
  three list items and pressing `Alt+Up` moves all three. With nothing
  selected it applies to the block holding the caret.
- `Ctrl+Left`/`Ctrl+Right` move by word, `Ctrl+Home`/`Ctrl+End` jump to the ends
  of the note, `PageUp`/`PageDown` move by a screenful, and holding `Shift` with
  any movement extends the selection.
- Typing `[] `, `[ ] ` or `[x] ` at the start of a line writes a real task
  marker. Every other Markdown shortcut (`# `, `- `, `1. `, `> `, `---`,
  ` ``` `) is already the syntax it looks like, so it is left exactly as typed.
- A run of `>` lines is drawn as one quote or one callout rather than one per
  line. `> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]` and `[!CAUTION]`
  each get their own colour and a badge; the slash menu and the turn-into menu
  offer all five.
- A fenced code block shows its language and a `Copy` button in its top right.
- A note can carry an `icon:` in its front matter, shown beside it in the tree
  and the breadcrumb. It names one of the marks the shell draws -- `star`,
  `check`, `flag`, `bookmark`, `tag`, `folder`, `calendar`, `clock`, `bolt`,
  `warning`, `code` -- and is chosen from a grid, not typed. The first cell of
  that grid is "none", which removes the key. Front matter keys micronotes does
  not model are preserved exactly as they were written, so a note written by
  another tool survives being saved here.
- `Ctrl+1`: raw Markdown source, which is where you type.
- `Ctrl+2`: reading view. Markdown is rendered rather than shown as source:
  headings, emphasis, code spans, links, list bullets and task checkboxes all
  show as formatting, and the syntax markers are hidden. Text is selectable,
  links and task checkboxes are clickable, and a fenced block keeps its `Copy`
  button. Tables, raw HTML, footnote definitions and indented code render
  through the `md4c` model.
- `Ctrl+3`: source beside the reading view. This is the default.
- `Ctrl+L`: cycle through the three pane modes.
- `Ctrl+Shift+L`: switch between the light and dark theme. The choice is stored
  in the library's `.micronotes/ui.state`.
- `/`: focus search when the editor is not focused.
- Click in the editor to place the cursor.
- Right-click a note anywhere in the sidebar - in the tree or in a list of
  search results - for Rename, Set icon, Edit tags, Move, Delete, and the four
  questions about the note as a *file*: Show on disk, Copy relative path, Copy
  absolute path, and Export as PDF. The three path commands are also on the
  menu bar's Note menu and in the command palette. A relative path is relative
  to the library root, which is what makes it the one worth having: it is the
  spelling that means the same thing to somebody else looking at the same
  library.
- Export as PDF is on the notebook menu too, where it means the notebook and
  everything under it bound into one file, one note per page in tree order.
  Both open the desktop's own save dialog. The page is A4 with the note's title
  and tags at the top, a running header on continuation pages and a page number
  at the foot, and it is always set light on white whatever theme the window is
  in - a dark-theme PDF is a rectangle of ink on every sheet that goes through
  a printer. The text is real text rather than a picture of it: it can be
  selected, copied and searched, and the vendored faces are embedded so it
  looks the same on a machine that does not have them - cut down to the glyphs
  the note actually shows, which is the difference between a 113 KB
  one-paragraph note and a 489 KB one. It is kerned as well: the pair spacing
  the faces ask for is read out of their `GPOS` tables and written into the
  page, so a heading on paper has the letter fit it has on screen. Links are
  live where they can be - an `https://` target becomes a clickable annotation,
  and a link to a note or a file inside the library does not, because neither
  is anywhere a reader of the PDF can go. Pictures come along - a JPEG is
  passed through untouched, anything else is decoded - and a table breaks
  between its rows rather than running off the page.
- Right-click a tab for Close, Pin and those same three path commands. The menu
  is about the tab under the pointer rather than the note on the page, and
  opening it does not switch to that tab. `Ctrl+click` a tab also pins it; a
  pinned tab is never the one replaced by the next note opened.
- Right-click a tag - on its row in the sidebar's Tags band, or on any coloured
  dot at the trailing edge of a note's row - to filter by it or to choose its
  colour from a palette of twelve. Hovering a dot names its tag; clicking one
  filters by it.
- Click a section band - Notebooks, Favorites, Tags, Recent - to shut it, or
  arrow onto it and use `Left`/`Right`. `Enter` on a sidebar row chooses it,
  which is how a tag or a band is acted on from the keyboard; arrowing over one
  deliberately does not, since either would replace the list being walked.
- The search field sits at the top of the sidebar, above the navigation it
  filters. `Ctrl+Shift+F` focuses it, and the `A`/`T`/`C` button at its right
  cycles the scope between all, title only and content only. While a query is
  running the sidebar lists the matching notes and up to three matching lines
  each instead of the tree; `Up`/`Down` walk the results while the field keeps
  the typing, and `Esc` clears the query and brings the tree back. Selecting a
  tag does the same thing with that tag's notes.
- A matching line is marked where it matched, in the same fill find-in-note
  uses, and a line too long for the column is trimmed around its match rather
  than from its end - so the match is always the part that stays on screen. A
  query is matched literally: `%` and `_` are ordinary characters, not
  wildcards.
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
- The window draws its own menu bar rather than wearing the compositor's title
  bar: File, Edit, View, Go, Note and Help, the application's name centred when
  there is room for it, and minimize / maximize / close at the right. Menus that
  do not fit a narrow window hide behind a chevron rather than being dropped.
  Sliding along the bar with one open switches menus without a click; the arrow
  keys walk an open menu and Escape shuts it. Dragging the empty part of the bar
  moves the window, and the window's edges resize it. On a platform that refuses
  a hit test the ordinary decorations come back and the drawn buttons stand down.
- Every menu item is an entry in `ui::Actions` and nothing else, so an item, its
  palette row and its keyboard chord cannot drift apart -- they are one row in
  one table, drawn three ways. `ArchitectureTests` proves every action the menus
  or the palette offer actually reaches a command.
- Under the tab strip, over the page, a breadcrumb band names the notebooks down
  to the open note (click one to go there) and carries the star that pins it to
  Favorites. It belongs to the page's own column rather than spanning the window,
  so the trail sits directly over the note it describes rather than over the tree.
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
  text in it - says which of those it is and which key does something about it,
  and wraps rather than truncating: half a sentence in a narrow panel says less
  than nothing.
- The icon rail does not hide, and a window too short to hold all seven of its
  controls drops them in a stated order - Settings first, the sidebar toggle
  last - so a shell with every panel put away always keeps the button that
  brings them back.

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

Note icons are **drawn**, not typeset: eleven marks in `ui/Draw.cpp`, rendered
with the same lines and rects as the chevrons, the tick and the favourite star.

They used to be emoji, and could not stay. A colour emoji font such as Noto
Color Emoji carries a single fixed bitmap strike - 128 pixels - which SDL3_ttf
cannot resize, so every icon had to be resampled down to the sixteen pixels a
sidebar row gives it, which is exactly where a resampled bitmap looks worst. On
a machine with no emoji font installed there was nothing to resample: the note
kept its default page mark and the picker appeared to have done nothing. A
drawn mark is exact at any size and is the same on every machine, which is what
an icon in a shared library has to be.

Emoji typed into a note's *text* are a different matter and still render: a
colour emoji face cannot be attached to the body font for the same
fixed-strike reason, so the monochrome Noto Emoji is attached when it is
installed and the colour one is not.

An `icon:` value this version does not recognise - an emoji written by an older
one, or a name from a future set - is left in the file untouched and the note
shows the default page mark until its icon is set again.

## Debug And Capture Flags

These exist to make UI work reproducible and are not part of normal use:

```bash
# Render one frame to a PNG and exit.
./build/bin/micronotes --library ~/Notes --screenshot /tmp/shot.png

--size 1400x900        # window size
--theme light|dark     # override the stored theme
--scale 2              # override the display scale
--pane editor|viewer|split
--select <title>       # open the first note whose title contains this
--search <query>       # seed the sidebar's search field, to capture it searching
--find <query>         # open the find bar over the note with this needle in it
--panels sidebar,right # which side panels to show, rather than whatever was stored
--right-panel outline|backlinks|links|tags   # "links" is the tab's own label
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
