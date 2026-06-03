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
- `Ctrl+V`: paste text, or paste clipboard image data as a managed image attachment.
- `Ctrl+Z`, `Ctrl+Y`: undo and redo editor changes.
- `Ctrl+1`, `Ctrl+2`, `Ctrl+3`: editor, viewer, and split panes.
- `/`: focus search when the editor is not focused.
- Click in the editor to place the cursor.
- Right-click a note in the note list for Rename and Delete actions.
- Drag a local file onto the editor to copy it into managed attachments and insert a Markdown link.
- `Esc`: clear search focus and return to the editor.

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
