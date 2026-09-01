#include "ui/WorkspaceModel.h"

#include <algorithm>

namespace micronotes::ui {

std::string_view rightPanelViewName(RightPanelView view) {
  switch(view) {
    case RightPanelView::Outline: return "outline";
    case RightPanelView::Backlinks: return "backlinks";
    case RightPanelView::Tags: return "tags";
  }
  return "outline";
}

RightPanelView rightPanelViewFromName(std::string_view name) {
  if(name == "backlinks") return RightPanelView::Backlinks;
  if(name == "tags") return RightPanelView::Tags;
  return RightPanelView::Outline;
}

PaneMode WorkspaceModel::paneMode() const {
  const auto* tab = activeTab_();
  return tab ? tab->paneMode : PaneMode::Live;
}

void WorkspaceModel::setPaneMode(PaneMode mode) {
  if(auto* tab = activeTab_()) tab->paneMode = mode;
}

const NoteTab* WorkspaceModel::activeTab_() const {
  return activeTab < tabs.size() ? &tabs[activeTab] : nullptr;
}

NoteTab* WorkspaceModel::activeTab_() {
  return activeTab < tabs.size() ? &tabs[activeTab] : nullptr;
}

std::size_t WorkspaceModel::findTab(std::string_view noteId) const {
  for(std::size_t i = 0; i < tabs.size(); ++i) {
    if(tabs[i].noteId == noteId) return i;
  }
  return std::string::npos;
}

void WorkspaceModel::openNote(const std::string& noteId, bool inNewTab) {
  if(noteId.empty()) return;
  // Already open: go to it. Opening a second tab on the same note is never what
  // was meant by clicking its name.
  if(const auto found = findTab(noteId); found != std::string::npos) {
    activeTab = found;
    return;
  }
  NoteTab tab;
  tab.noteId = noteId;
  // The new tab inherits how you were looking at the last one, so a reader who
  // works in the reading view stays in it.
  tab.paneMode = paneMode();
  auto* active = activeTab_();
  if(!inNewTab && active && !active->pinned) {
    *active = tab;
    return;
  }
  tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(std::min(activeTab + 1, tabs.size())), tab);
  activeTab = std::min(activeTab + 1, tabs.size() - 1);
  if(tabs.size() == 1) activeTab = 0;
}

void WorkspaceModel::closeTab(std::size_t index) {
  if(index >= tabs.size()) return;
  tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(index));
  if(tabs.empty()) {
    activeTab = 0;
    return;
  }
  // Closing a tab to the left of the active one must not change which note is
  // showing, so the index follows its own tab rather than staying put.
  if(index < activeTab) --activeTab;
  if(activeTab >= tabs.size()) activeTab = tabs.size() - 1;
}

void WorkspaceModel::stepTab(int delta) {
  if(tabs.size() < 2) return;
  const auto count = static_cast<int>(tabs.size());
  int next = (static_cast<int>(activeTab) + delta) % count;
  if(next < 0) next += count;
  activeTab = static_cast<std::size_t>(next);
}

ShellLayoutInputs WorkspaceModel::layoutInputs(float windowWidth, float windowHeight,
                                               LayoutMode previousMode) const {
  ShellLayoutInputs inputs;
  inputs.windowWidth = windowWidth;
  inputs.windowHeight = windowHeight;
  inputs.sidebarVisible = sidebarVisible;
  inputs.noteListVisible = noteListVisible;
  inputs.rightPanelVisible = rightPanelVisible;
  inputs.sidebarWidth = sidebarWidth;
  inputs.noteListWidth = noteListWidth;
  inputs.rightPanelWidth = rightPanelWidth;
  inputs.previousMode = previousMode;
  return inputs;
}

bool WorkspaceModel::togglePanel(bool WorkspaceModel::*panel) {
  // Hiding the last panel that can reach another note would leave the palette
  // as the only way out, so the last one standing refuses. The right panel is
  // not counted: it shows the open note, it does not navigate to one.
  const bool hiding = this->*panel;
  if(hiding && panel != &WorkspaceModel::rightPanelVisible) {
    const bool otherShowing = panel == &WorkspaceModel::sidebarVisible ? noteListVisible : sidebarVisible;
    if(!otherShowing) return false;
  }
  this->*panel = !(this->*panel);
  return true;
}

}
