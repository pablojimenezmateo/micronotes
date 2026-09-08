#pragma once

#include "ui/CaretBlink.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <optional>

// What the reader is doing to the note: what is selected, what is being
// dragged, and where the caret is in its blink.
//
// Twenty loose fields on `UiRuntime` before this, and they held three gestures
// that look alike and behave differently. Named apart, the difference is
// visible in the types: a *text* selection is one anchor plus the editor's own
// cursor, a *block* selection is two source offsets, and a block *drag* is two
// more offsets plus a drop point that may not exist yet.
//
// Block offsets rather than block indices throughout, deliberately: an edit
// underneath a selection changes which block a given index names, and holding
// indices would let a selection silently re-point at a different block.
namespace micronotes::app {

// Extending a selection by dragging, in the note or in a one-line field.
//
// Two of these rather than one, because they can both be live in the same
// frame -- a drag that starts in the search box and leaves it -- and because
// the anchor means a different thing in each: a byte offset into the note, and
// a character index into the field.
struct DragSelect {
  bool active = false;
  std::size_t anchor = 0;
};

// Blocks selected as objects: the arrows walk them, and one command acts over
// the whole range.
struct BlockSelection {
  bool active = false;
  std::size_t anchor = 0;
  std::size_t focus = 0;

  void clear() { active = false; }
};

// A block selection being dragged to a new position. Separate from the
// selection it moves, because the selection stays where it is until the button
// comes up: a drag that is abandoned has to leave the note untouched.
struct BlockDrag {
  bool active = false;
  std::size_t anchor = 0;
  std::size_t focus = 0;
  // Where the blocks would land. Absent until the pointer is over somewhere
  // they could go, which is what tells the release there is nothing to commit.
  std::optional<std::size_t> dropOffset;
};

// The block inserter, opened by typing "/" or by the gutter's insert button.
//
// Three fields with one contract between them, and only `openSlashMenu` and
// `commitSlashMenu` are meant to know it: `start` is where the "/" was typed
// and committing erases back to it, unless `inserts` is set, in which case
// nothing is erased and the new block goes after `afterBlock` instead. See
// `app/BlockMenus.h`.
struct SlashMenu {
  std::size_t start = 0;
  bool inserts = false;
  std::size_t afterBlock = 0;
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
  // What the last painted frame actually drew. The blink is the only thing in
  // the shell that changes with no event behind it, so it is the only thing
  // whose "is a repaint due" question cannot be answered by the event queue --
  // see `caretPhaseChanged`.
  bool painted = true;
};

// A double or triple click, as a count and the moment it last landed.
//
// On its own because "did this click continue the last one" is a rule about
// time, and it was written out twice -- once for the live surface and once for
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
