#pragma once

#include "core/ui/ShellModel.h"
#include "ui/Metrics.h"
#include "ui/ShellLayout.h"

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
  PaneMode paneMode = PaneMode::Live;

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

  // The window is the only thing the model does not already know.
  ShellLayoutInputs layoutInputs(float windowWidth, float windowHeight, LayoutMode previousMode) const;

  // Hiding every panel would leave no way back to another note but the palette,
  // so the last one standing refuses to go. Returns whether anything changed.
  bool togglePanel(bool WorkspaceModel::*panel);
};

}
