#pragma once

#include "core/ui/ShellModel.h"
#include "ui/Metrics.h"
#include "ui/ShellLayout.h"
#include "ui/Tabs.h"

#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// How a note is being looked at. Genuinely shared with microagenda, so it stays
// in the vendored core; everything below is a notes-app idea and does not.
using microcore::ui::PaneMode;

// What the right-hand panel is showing. All three are views of the open note
// rather than of the library, which is why they share one panel instead of
// becoming three.
enum class RightPanelView {
  Outline,
  Backlinks,
  Tags
};

std::string_view rightPanelViewName(RightPanelView view);
RightPanelView rightPanelViewFromName(std::string_view name);

// The arrangement of the window: which panels are showing, how wide they are,
// and what the reader keeps to hand.
//
// This deliberately does not extend microcore::ui::ShellModel, even though the
// two widths started there. That struct is vendored byte-for-byte into
// microagenda and hash-checked by a ctest, so every edit to it is a four-step
// dance across two repositories and a stale copy fails the whole suite rather
// than one test. Panels, and the tabs and splits that follow, are things a
// notes app has and an agenda does not; microagenda would inherit code it never
// calls. PaneMode is the one piece that really is shared, so that is the one
// piece still imported.
struct WorkspaceModel {
  // The notes open in the editor area, and which of them is showing. Never
  // empty is not an invariant worth having: a window with nothing open is a
  // real state, and the empty-strip case is simpler than a sentinel tab.
  std::vector<NoteTab> tabs;
  std::size_t activeTab = 0;

  // A hidden panel keeps its width, so showing it again restores the size it
  // had rather than snapping to the default.
  bool sidebarVisible = true;
  bool noteListVisible = true;
  bool rightPanelVisible = false;
  RightPanelView rightPanelView = RightPanelView::Outline;

  float sidebarWidth = kDefaultSidebarWidth;
  float noteListWidth = kDefaultNoteListWidth;
  float rightPanelWidth = kDefaultRightPanelWidth;

  // Notes pinned to the top of the sidebar, and the ones opened most recently,
  // newest first. Both name notes by id and never touch a file: which notes
  // someone keeps to hand is a view preference, not part of the note.
  std::vector<std::string> favorites;
  std::vector<std::string> recents;

  // How the note on screen is being looked at. Reads the active tab, and falls
  // back to the default when nothing is open, so callers never have to ask
  // whether there is a tab first.
  PaneMode paneMode() const;
  void setPaneMode(PaneMode mode);

  const NoteTab* activeTab_() const;
  NoteTab* activeTab_();

  // The tab showing `noteId`, or npos.
  std::size_t findTab(std::string_view noteId) const;

  // Opens a note. Without `inNewTab` it replaces the active tab -- which is
  // what a single click in the sidebar means -- unless that tab is pinned, in
  // which case a new one is opened rather than the pin being ignored. A note
  // that is already open is switched to rather than opened twice.
  void openNote(const std::string& noteId, bool inNewTab);

  // Closes a tab and picks the next one to show: the tab to the right, or the
  // one to the left when the closed tab was last, which is what every editor
  // does and what keeps a run of closes moving in one direction.
  void closeTab(std::size_t index);

  // Moves `delta` tabs along, wrapping. Doing nothing when fewer than two are
  // open keeps the shortcut from looking broken.
  void stepTab(int delta);

  // The window is the only thing the model does not already know.
  ShellLayoutInputs layoutInputs(float windowWidth, float windowHeight, LayoutMode previousMode) const;

  // Hiding every panel would leave no way back to another note but the palette,
  // so the last one standing refuses to go. Returns whether anything changed.
  bool togglePanel(bool WorkspaceModel::*panel);
};

}
