#pragma once

#include "CoreAliases.h"

#include "core/util/TextSearch.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What the find bar is looking for, what it found, and which one you are on.
//
// A surface's state in its own header, like `app/SidebarState.h` and the rest.
// The *needle* is not here: it is `ui.fields.find`, one of the five one-line
// fields whose routing is a function of `ui.focus` (see `app/TextFields.h`), and
// splitting it out would give the find bar two homes. What is here is everything
// that is a consequence of the needle.
//
// Before this the find bar had no state at all. It was a text field plus a
// number in the status line, and the three surfaces that highlighted its query
// each searched the note for themselves -- so there was nothing to be "on", no
// way to step from one match to the next, and no place to put a `match case`
// toggle even if one had been wanted. The matches being a value the shell owns
// is what makes all three possible.
namespace micronotes::app {

// A memo, spelled out rather than a `ui::Memo`, and the reason is not the one
// that was written here. It said `Memo::store` takes its value by value and so
// would free and re-grow the match vector on every keystroke; `Memo::rebuild`
// exists for exactly that and hands the stored value back to fill in place.
//
// The reason it stays hand-rolled is `forget`. Three surfaces read
// `FindState::matches` straight to paint their highlights, and when the note
// being searched is replaced those offsets address a buffer that is gone -- so
// forgetting has to *empty* the vector, not merely mark it stale. `Memo` is
// deliberately the other way round: `value()` answers whatever is held whether
// or not it is current. A memo whose stale value is dangerous to read is not
// this shape.
//
// The key is still compared and assigned whole, which is the failure `ui::Memo`
// exists to prevent, so what is left here is the safe half of that shape.
struct FindMatchKey {
  std::uint64_t revision = 0;
  std::string needle;
  util::SearchOptions options;

  friend bool operator==(const FindMatchKey&, const FindMatchKey&) = default;
};

struct FindState {
  // Whether the bar is showing. Separate from `ui.focus == FocusArea::Find`,
  // because clicking into the note to look at a match must not close the bar or
  // drop the highlights -- which is what tying the two together would mean, and
  // what the status-line find did.
  bool open = false;

  util::SearchOptions options;

  // Every match in the buffer, ascending, non-overlapping. Read by the two page
  // surfaces and the raw pane to paint their highlights, so they cannot disagree
  // with the count in the bar or with each other.
  std::vector<util::TextMatch> matches;
  // Which one the reader is on. Meaningless when `matches` is empty; kept in
  // range by everything that touches it, so a caller may index with it after
  // checking `matches` is not.
  std::size_t active = 0;
  // The scan hit `util::kMaxMatches`, so the count is a floor. The bar says so
  // rather than lying with a round number.
  bool truncated = false;

  FindMatchKey key;
  bool valid = false;

  bool hasMatches() const { return !matches.empty(); }

  // The active match, or nothing to highlight. `npos` rather than an optional
  // because the two page surfaces take it as a plain index alongside the span.
  static constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);
  std::size_t activeIndex() const { return matches.empty() ? kNoMatch : active; }

  // Forgets the match set without closing the bar. Called when the note being
  // searched is replaced: the offsets in `matches` address a buffer that is no
  // longer there, and a highlight drawn from them lands on arbitrary bytes of
  // the new note.
  void forget() {
    matches.clear();
    active = 0;
    truncated = false;
    valid = false;
    key = {};
  }
};

}
