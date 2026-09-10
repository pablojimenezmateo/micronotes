#pragma once

#include "ui/CaretBlink.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <cstddef>

// What the reader is doing to the note: what is selected, and where the caret
// is in its blink.
//
// Twenty loose fields on `UiRuntime` before this, and they held gestures that
// look alike and behave differently. Named apart, the difference is visible in
// the types: a selection in the note is one anchor plus the editor's own
// cursor, a selection in a one-line field is an anchor plus a character index.
namespace micronotes::app {

// Extending a selection by dragging, in the note or in a one-line field.
//
// Two of these rather than one, because they can both be live in the same
// frame -- a drag that starts in the search box and leaves it -- and because
// the anchor means a different thing in each: a byte offset into the note, and
// a character index into the field.
// Which surface a note-text drag is being made on.
//
// The motion that extends a selection has to map the pointer through the same
// page the press did, and it used to re-decide that per event from the pane
// mode. In split view that named the raw pane for a drag begun in the reading
// pane, so dragging in the reader selected whatever those coordinates meant in
// a differently wrapped column beside it.
enum class SelectSurface {
  RawPane,
  ReadingPage
};

struct DragSelect {
  bool active = false;
  std::size_t anchor = 0;
  SelectSurface surface = SelectSurface::RawPane;
};

// The block inserter, opened by typing "/" or from the palette.
//
// `start` is where the "/" was typed and committing erases back to it -- one
// contract, and only `openSlashMenu` and `commitSlashMenu` are meant to know
// it. See `app/BlockMenus.h`.
struct SlashMenu {
  std::size_t start = 0;
};

// Where the caret is, and whether it is showing.
struct CaretState {
  // In window coordinates, published to SDL each frame so the IME can position
  // its candidate window. Zero-sized until the editor draws.
  SDL_Rect rect {0, 0, 0, 0};
  bool reported = false;

  // Every caret in the shell was drawn solid, which in a mono chrome face reads
  // as a pipe character rather than as an insertion point -- so the one thing
  // whose job is to say "your typing goes here" said it in the same voice as
  // the text around it. See `ui::CaretBlink`; `caretStateKey` is what it is fed.
  ui::CaretBlink blink;
  bool visible = true;
  // Held solid, whatever the clock says. One caller: `--screenshot`, which
  // paints for about half a second before it reads the window back, and the
  // blink's half-period is 530 ms -- so which phase the capture landed in
  // depended on how long startup took. `tools/session-compare.sh` compares two
  // builds by `cmp` of their screenshots, and a coin-flip on two pixel columns
  // made the instrument report a difference roughly one run in six.
  bool frozen = false;
  // What the last painted frame actually drew. The blink is the only thing in
  // the shell that changes with no event behind it, so it is the only thing
  // whose "is a repaint due" question cannot be answered by the event queue --
  // see `caretPhaseChanged`.
  bool painted = true;
};

// A double or triple click, as a count and the moment it last landed.
//
// On its own because "did this click continue the last one" is a rule about
// time, and it was written out twice -- once for the note page and once for
// the raw pane -- with the 450 ms threshold spelled out at both.
struct ClickRun {
  Uint64 lastAt = 0;
  int count = 0;

  // Counts this click as a continuation when it lands soon enough after the
  // last, and reports how many are now in the run.
  int extend(Uint64 now) {
    count = now - lastAt < kDoubleClickMs ? count + 1 : 1;
    lastAt = now;
    return count;
  }
  void reset() { count = 0; }

private:
  static constexpr Uint64 kDoubleClickMs = 450;
};

}
