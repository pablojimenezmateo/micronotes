#pragma once

#include "core/ui/ShellModel.h"
#include "ui/Metrics.h"
#include "ui/TagColors.h"
#include "ui/ShellLayout.h"
#include "ui/Tabs.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// How a note is being looked at. An editing-surface idea rather than a
// notes-app one, so it lives in the core; everything below is a notes-app idea
// and does not.
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

// The bands the sidebar's row list is divided into.
//
// They were four headings in one uninterrupted column -- FAVORITES, the tree,
// TAGS, RECENT -- set in the same muted ink and on the same ground as the rows
// they headed, and the tree had no heading at all. So the panel read as one
// long list with some words in it, and where TAGS stopped and RECENT began was
// something the reader had to work out from the shape of the entries. Each is a
// band of its own now, and each can be shut.
//
// The tree is one of them. Without a band of its own it was an unlabelled
// section between two labelled ones, which is most of what made the division
// unclear: naming three of four groups is worse than naming none.
enum class SidebarSection {
  Notebooks,
  Favorites,
  Tags,
  Recent
};

std::string_view sidebarSectionName(SidebarSection section);
// Every section, in the order the sidebar stacks them. For the persistence and
// the tests, so neither has to restate the list.
const SidebarSection* sidebarSections(std::size_t* count);

// The arrangement of the window: which panels are showing, how wide they are,
// and what the reader keeps to hand.
//
// This deliberately does not extend microcore::ui::ShellModel, even though the
// two widths started there. Panels, and the tabs and splits that follow, are
// notes-app furniture: putting them in the core would mean the app-agnostic
// layer describing an arrangement only this app has. PaneMode is the one piece
// that genuinely is not app-specific, so that is the one piece imported.
struct WorkspaceModel {
  // The notes open in the editor area, and which of them is showing. Never
  // empty is not an invariant worth having: a window with nothing open is a
  // real state, and the empty-strip case is simpler than a sentinel tab.
  std::vector<NoteTab> tabs;
  std::size_t activeTab = 0;

  // A hidden panel keeps its width, so showing it again restores the size it
  // had rather than snapping to the default.
  bool sidebarVisible = true;
  bool rightPanelVisible = false;
  RightPanelView rightPanelView = RightPanelView::Outline;

  float sidebarWidth = kDefaultSidebarWidth;
  float rightPanelWidth = kDefaultRightPanelWidth;

  // Notes pinned to the top of the sidebar, and the ones opened most recently,
  // newest first. Both name notes by id and never touch a file: which notes
  // someone keeps to hand is a view preference, not part of the note.
  std::vector<std::string> favorites;
  std::vector<std::string> recents;

  // A colour per tag, which is what joins a note's row to the tags it carries.
  // A view preference like the two above and, like them, never written into a
  // note: what colour somebody finds `#work` easiest to spot is not part of the
  // library. See `ui::TagColors`.
  TagColors tagColors;

  // Which sidebar bands are shut. A view preference too, and the reason it is
  // on the model rather than on the runtime: a reader who shuts TAGS because
  // their library has sixty of them wants it shut again next time.
  bool sectionCollapsed(SidebarSection section) const;
  void setSectionCollapsed(SidebarSection section, bool collapsed);
  void toggleSection(SidebarSection section);

  // How the note on screen is being looked at. Reads the active tab, and falls
  // back to the default when nothing is open, so callers never have to ask
  // whether there is a tab first.
  PaneMode paneMode() const;
  void setPaneMode(PaneMode mode);

  const NoteTab* activeTab_() const;
  NoteTab* activeTab_();

  // The tab showing `noteId`, or npos.
  std::size_t findTab(std::string_view noteId) const;

  // Opens a note in a tab of its own, beside the one showing. A note that is
  // already open is switched to rather than opened twice -- clicking a name is
  // never a request for a duplicate.
  //
  // `TabPolicy::Reuse` takes over the tab showing instead, unless it is pinned.
  // Exactly one caller wants it: walking the keyboard cursor through the
  // sidebar, which opens each note it passes over. See `TabPolicy`.
  //
  // At `kMaxTabs` the leftmost tab that is neither pinned nor active gives way.
  void openNote(const std::string& noteId, TabPolicy policy = TabPolicy::NewTab);

  // Re-points every tab showing `from` at `to`. A note's id changes exactly
  // once in its life: the first time micronotes saves a file that arrived
  // without front matter, it stops being filed under an id derived from its
  // path and gets a permanent one. Without this the tab still names the old id
  // and the note vanishes out from under the person editing it.
  void renameNote(std::string_view from, const std::string& to);

  // Closes a tab and picks the next one to show: the tab to the right, or the
  // one to the left when the closed tab was last, which is what every editor
  // does and what keeps a run of closes moving in one direction.
  void closeTab(std::size_t index);

  // Moves `delta` tabs along, wrapping. Doing nothing when fewer than two are
  // open keeps the shortcut from looking broken.
  void stepTab(int delta);

  // Brings the strip back inside `kMaxTabs`. Called by `openNote`; exposed so a
  // test can drive the ceiling without guessing how many opens reach it.
  void trimTabs();

  // The window is the only thing the model does not already know.
  ShellLayoutInputs layoutInputs(float windowWidth, float windowHeight, LayoutMode previousMode) const;

  // Hiding every panel would leave no way back to another note but the palette,
  // so the last one standing refuses to go. Returns whether anything changed.
  bool togglePanel(bool WorkspaceModel::*panel);

  // One flag per `SidebarSection`, indexed by it. A fixed array rather than a
  // set of named bools so that adding a band does not mean touching the
  // persistence, the sidebar and four accessors -- and so `sidebarSections`
  // above can be walked over both.
  std::array<bool, 4> collapsedSections {};
};

}
