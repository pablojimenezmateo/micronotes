#pragma once

#include "ui/Memo.h"
#include "ui/Rect.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The line along the bottom of the window.
//
// It said four things, and the widest of them was an echo of the last action --
// "Copied selection", "Refreshed library" -- standing there until something
// replaced it. A strip that spends most of its width telling you what you have
// already done is a strip you stop reading, and once you have stopped reading
// it the three useful things on it are gone too.
//
// It is a row of *segments* now, which is the shape the sibling ../microide's
// status bar has (`workspace/services/StatusBarService.h`) and the shape every
// status bar that gets read has: each one answers a standing question about the
// note or the view, each says what it means when hovered, and the two that
// stand for something you can change run it when clicked. The messages did not
// go -- a good many are failures -- but they are transient now and take the
// middle of the bar for a few seconds rather than a column of it forever. See
// `app/StatusLine.h`.
namespace micronotes::app {

struct UiRuntime;

// The segments, in the order they are laid out. The first three go against the
// leading edge and the last two against the trailing one, which is the split
// every status bar makes: what is true of the *document* on the left, what is
// true of the *view* on the right.
enum class StatusSegment : std::size_t {
  Save = 0,
  Position,
  Selection,
  Words,
  PaneMode,
  Count
};

inline constexpr std::size_t kStatusSegmentCount = static_cast<std::size_t>(StatusSegment::Count);
// The first segment that belongs to the trailing group. Everything before it is
// packed left, everything from it on is packed right.
inline constexpr std::size_t kFirstTrailingSegment = static_cast<std::size_t>(StatusSegment::Words);

// How a segment reads. Derived where the value is known rather than sniffed
// back out of the text at paint time -- ../microide's rule, and the reason its
// status bar can colour a segment without parsing its own label.
enum class StatusTone {
  Normal,
  // The one thing on this bar that is a state rather than a fact: a note with
  // unsaved changes in it.
  Unsaved
};

struct StatusSegmentValue {
  std::string text;
  std::string tooltip;
  // The action a click runs, by `ui::ActionSpec::name`. Empty for a segment
  // that only reports. `clickable` is derived from it rather than being a
  // second field: a segment that lifts under the pointer and does nothing when
  // pressed is worse than one that never invited the press.
  std::string command;
  StatusTone tone = StatusTone::Normal;
  bool visible = false;

  bool clickable() const { return !command.empty(); }
};

using StatusSegments = std::array<StatusSegmentValue, kStatusSegmentCount>;

// What the bar says, as a value. Pure over the shell, so the test reads the
// strings without a window -- which is the whole reason it is not written
// inline in the paint.
StatusSegments statusSegments(const UiRuntime& ui);

// Where the caret is, as a reader counts: lines and columns from one, columns
// in code points rather than bytes so a line of accented text does not report a
// column past its own end.
//
// Its own type because it is memoised: counting the newlines before the caret
// is a pass over the note, and the bar is painted on every frame -- a scroll, a
// hover, the caret's own blink. The memo means it is recomputed when the buffer
// or the caret moves and not otherwise. Public for the test.
struct CaretPlace {
  std::size_t line = 1;
  std::size_t column = 1;
  // Where the caret's line begins. Nothing shows it. It is here because it is
  // what makes the *next* answer cheap: a place already known is an anchor, and
  // the line at any other offset is this line plus or minus the line breaks
  // between the two -- so the bar walks from where the caret was rather than
  // from the note's first byte.
  std::size_t lineStart = 0;
};

struct CaretPlaceKey {
  std::uint64_t revision = 0;
  std::size_t cursor = 0;

  friend bool operator==(const CaretPlaceKey&, const CaretPlaceKey&) = default;
};

// What a selection is worth saying: how many characters it covers. Code points
// again, and memoised for the same reason -- a drag across a long note would
// otherwise walk the whole selection on every motion event.
struct SelectionSpanKey {
  std::uint64_t revision = 0;
  std::size_t start = 0;
  std::size_t end = 0;

  friend bool operator==(const SelectionSpanKey&, const SelectionSpanKey&) = default;
};

CaretPlace caretPlaceIn(std::string_view text, std::size_t cursor);

// The same answer, walked from one already known for the same buffer.
//
// `anchor.lineStart` must be a real line start in `text` with `anchor.line - 1`
// line breaks before it; the caller is what establishes that, and gets it wrong
// by handing over a place taken from some other buffer. What this costs is the
// distance between the anchor and the caret rather than the offset of the
// caret, which for typing and for arrowing is a line and for the note's first
// paint is the whole note either way.
CaretPlace caretPlaceFrom(std::string_view text, std::size_t cursor, const CaretPlace& anchor);
std::size_t codePointsIn(std::string_view text, std::size_t start, std::size_t end);

// The bar's own memos, held by the shell because the bar is a function rather
// than an object. See `app/Shell.h`.
//
// `mutable` because asking what the bar says changes nothing a caller can
// observe -- `statusSegments` takes the runtime by const reference so a test
// can read the strings off a shell it is not allowed to disturb, and the memo
// is the reason that stays cheap.
struct StatusBarState {
  mutable ui::Memo<CaretPlace, CaretPlaceKey> caretPlace;
  mutable ui::Memo<std::size_t, SelectionSpanKey> selectionSize;
};

void drawStatus(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

// Whether a clickable segment is under the point. What the cursor shape asks,
// so the hand the pointer turns into and the command a press runs come from one
// geometry -- a bar that promises a click it does not honour is worse than one
// that never invited it.
bool statusBarHasControlAt(ui::TextRenderer& text, const UiRuntime& ui, ui::Rect rect, float x,
                           float y);

// A press on the bar. Runs the segment's command, if it has one, and reports
// whether the bar took the press -- it takes every press inside itself, so a
// click on the strip cannot fall through to whatever is behind it.
bool pressStatusBar(ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect, float x, float y,
                    Uint8 button);

}
