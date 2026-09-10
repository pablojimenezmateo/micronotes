#pragma once

#include "core/ui/PaneMode.h"
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
  microcore::ui::PaneMode paneMode = microcore::ui::PaneMode::Split;
  // A pinned tab is never the one evicted to make room, and never the one a
  // cursor move takes over.
  bool pinned = false;
  // How far down each of the two surfaces this note was left.
  //
  // On the tab for the same reason the pane mode is: it is a fact about how you
  // are looking at *this* note, and a window-wide scroll offset is wrong the
  // moment two notes are open. Switching tabs used to carry the offset over
  // from the note you left -- so leaving one note half way down and going back
  // to a short one landed past its end -- and switching by clicking the
  // sidebar reset it to the top instead, which is the same bug read from the
  // other side: the offset belonged to the window rather than to the note.
  //
  // Two, because the two panes lay the same note out differently and both can
  // be on screen at once in split view. Each counts in the unit its own surface
  // scrolls by: pixels for the reading pane, whole rows for the raw one.
  //
  // Not written to the session file, unlike the pane mode beside it. A pixel
  // offset only means anything against a layout, and by the next run the note
  // may have been edited by something else entirely -- so a remembered offset
  // would put a restored session somewhere nobody had scrolled to.
  int readingScroll = 0;
  int rawScroll = 0;

  friend bool operator==(const NoteTab&, const NoteTab&) = default;
};

// Whether opening a note adds a tab or takes over the one already showing.
//
// Opening a note gives it a tab of its own. The one exception is walking the
// keyboard cursor through the sidebar, which opens every note it passes over so
// that arrowing through the tree shows you each one -- holding Down through a
// thousand-note library would otherwise be a thousand tabs. So `Reuse` is not
// a preference, it is what stops a *browse* from being a thousand opens.
enum class TabPolicy { NewTab, Reuse };

// How many notes may be open at once.
//
// Every note opening in its own tab means the strip grows as you read, and it
// has to stop somewhere: `tabTitles` resolves a note per tab twice a frame, and
// a strip of hundreds is not a navigation device whatever it costs. At the
// ceiling the leftmost tab that is neither pinned nor showing gives way, so
// pinning is how a tab is kept -- which is what pinning already meant.
inline constexpr std::size_t kMaxTabs = 20;

// Where a tab strip put each tab, and where its close button went.
struct TabSlot {
  std::size_t index = 0;
  Rect rect;
  Rect close;
  // False when the strip ran out of room before reaching this tab.
  bool visible = false;
};

// Everything the strip drew, so the paint and the hit test read one answer.
struct TabStripLayout {
  std::vector<TabSlot> slots;
  // The chevron buttons at either end, and how many tabs each hides. Empty
  // rects and a zero count when the strip fits, so a strip of two notes has no
  // furniture at all.
  Rect scrollLeft;
  Rect scrollRight;
  std::size_t hiddenLeft = 0;
  std::size_t hiddenRight = 0;
};

// How wide a tab may be, and what it reserves. A strip of eight notes should
// still show enough of each title to tell them apart; a strip of two should not
// stretch them across the window.
//
// Measured-and-clamped rather than an even share of the strip: tabs that resize
// as tabs open and close mean the one you were about to click has moved.
inline constexpr float kTabPadding = 58.0f;
inline constexpr float kMinTabWidth = 132.0f;
inline constexpr float kMaxTabWidth = 220.0f;
inline constexpr float kTabClosePadding = 6.0f;
inline constexpr float kTabCloseSize = 14.0f;
// What a tab keeps clear at its trailing edge for the cross, whether or not one
// is currently drawn there: a title that reflows when the pointer arrives is a
// title that moves under the pointer.
inline constexpr float kTabCloseReserve = 40.0f;
// The chevron button at either end of an overflowing strip.
inline constexpr float kTabScrollButtonWidth = 28.0f;
// The close button's grab area is larger than the cross drawn in it. One
// constant, read by the paint and by the hit test alike, so the region that
// responds and the region that looks like it will can never drift apart.
inline constexpr float kTabCloseHitInflate = 3.0f;

// Lays a strip of tabs out left to right, scrolled to keep `activeTab` on
// screen.
//
// A pure function of the titles, the room available and which tab is active, so
// the geometry the strip is painted with is the same geometry a click is tested
// against -- and so both can be checked without a window. `measure` gives the
// width of a title in the strip's own font, and it is load-bearing: the widths
// depend on it, so a caller that omits it lays out a *different* strip.
//
// The scroll is derived rather than stored. A strip with more tabs than fit
// shows the window of them ending at the active tab, which needs no state to
// keep in step and cannot drift between the draw and the hit test. Tabs opened
// past the right edge used to be laid out and then simply not drawn, so they
// existed, were unreachable by mouse, and could only be got to with Ctrl+Tab.
TabStripLayout layoutTabs(const std::vector<std::string>& titles, Rect strip,
                          const std::function<int(std::string_view)>& measure,
                          std::size_t activeTab = 0);

// The close button of `slot`, grown by the hit inflate. Used by the hit test;
// the paint uses `slot.close` itself.
Rect tabCloseHitRect(const TabSlot& slot);

// --- dragging a tab into a new place ---------------------------------------

// How far the pointer has to travel from where it went down before the press
// becomes a drag rather than a click. Without it every click on a tab that
// twitched by a pixel would reorder the strip.
inline constexpr float kTabDragStartDistance = 6.0f;

// Where the dragged tab is drawn, pinned inside the strip.
//
// The pointer keeps the grip it took on the tab -- grabbing a tab by its right
// edge and having it jump so the pointer is at its middle is the thing that
// makes a drag feel like it is fighting you -- so this is the pointer less that
// grip, clamped to the strip. `std::clamp` on a strip narrower than the tab is
// undefined behaviour, which a very narrow window reaches, so the upper bound
// is raised to the lower one first.
float draggedTabX(Rect strip, float tabWidth, float pointerX, float grabOffsetX);

// The accent rule that marks the gap a dragged tab would drop into. Two pixels,
// the same as the lid over the active tab, so the strip has one accent weight.
inline constexpr float kTabDropCaretWidth = 2.0f;

// Which gap in the strip the dragged tab would drop into: the index it would
// take, so `0` is before the first tab and `tabCount` is past the last.
//
// Resolved from `x`, the leading edge of the *drawn* tab, rather than from the
// pointer. Keying it off the pointer makes the landing place depend on where
// inside the tab it was grabbed, so dragging a wide tab held near one edge
// feels like the strip is trailing half a tab behind the hand.
//
// Both ends pin to what is *visible*. A scrolled strip is a window onto the
// list, and answering 0 or `tabCount` for a drop past a visible edge would
// teleport the tab to a slot that is not on screen -- the chevrons are how the
// window moves.
std::size_t tabDropSlot(const TabStripLayout& layout, Rect strip, float x,
                        std::size_t tabCount);

// The index the tab at `from` ends up at when dropped into `slot`.
//
// A slot is a gap between tabs and an index is a tab, and the two differ by one
// on the right of the tab being moved: lifting tab 2 out and dropping it into
// the gap at 5 lands it at index 4, because tabs 3 and 4 each slid one to the
// left to fill the hole it left.
std::size_t tabIndexForDropSlot(std::size_t slot, std::size_t from, std::size_t tabCount);

// Moves the tab at `from` to `to`, keeping `activeTab` on whichever tab it
// already named. False on an out-of-range index; true otherwise, the
// `from == to` no-op included.
//
// One rotate rather than erase-then-insert: that pair moves every tab between
// the two positions twice, plus a tab hoisted out and back in.
bool moveTab(std::vector<NoteTab>& tabs, std::size_t& activeTab, std::size_t from,
             std::size_t to);

}
