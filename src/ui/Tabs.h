#pragma once

#include "core/ui/ShellModel.h"
#include "ui/Rect.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// One note open in the editor area.
//
// The pane mode belongs to the tab rather than to the window. It describes how
// you are looking at *this* note, and a single window-wide mode is wrong the
// moment two notes are open -- a table you are editing as raw Markdown should
// not turn the note beside it into source too.
struct NoteTab {
  std::string noteId;
  microcore::ui::PaneMode paneMode = microcore::ui::PaneMode::Live;
  // A pinned tab is never the one that gets replaced when a note is opened
  // without asking for a new tab.
  bool pinned = false;

  friend bool operator==(const NoteTab&, const NoteTab&) = default;
};

// Where a tab strip put each tab, and where its close button went.
struct TabSlot {
  std::size_t index = 0;
  Rect rect;
  Rect close;
  // False when the strip ran out of room before reaching this tab.
  bool visible = false;
};

// How wide a tab may be. A strip of eight notes should still show enough of
// each title to tell them apart; a strip of two should not stretch them across
// the window.
inline constexpr float kMinTabWidth = 110.0f;
inline constexpr float kMaxTabWidth = 220.0f;
inline constexpr float kTabClosePadding = 8.0f;
inline constexpr float kTabCloseSize = 14.0f;
// The close button's grab area is larger than the cross drawn in it. One
// constant, read by the paint and by the hit test alike, so the region that
// responds and the region that looks like it will can never drift apart.
inline constexpr float kTabCloseHitInflate = 3.0f;

// Lays a strip of tabs out left to right.
//
// A pure function of the titles and the room available, so the geometry the
// strip is painted with is the same geometry a click is tested against -- and
// so both can be checked without a window. `measure` gives the width of a title
// in the strip's own font.
std::vector<TabSlot> layoutTabs(const std::vector<std::string>& titles, Rect strip,
                                const std::function<int(std::string_view)>& measure);

// The close button of `slot`, grown by the hit inflate. Used by the hit test;
// the paint uses `slot.close` itself.
Rect tabCloseHitRect(const TabSlot& slot);

}
