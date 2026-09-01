#include "ui/WorkspaceModel.h"

namespace micronotes::ui {

std::string_view rightPanelViewName(RightPanelView view) {
  switch(view) {
    case RightPanelView::Outline: return "outline";
    case RightPanelView::Tags: return "tags";
  }
  return "outline";
}

RightPanelView rightPanelViewFromName(std::string_view name) {
  if(name == "tags") return RightPanelView::Tags;
  return RightPanelView::Outline;
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
