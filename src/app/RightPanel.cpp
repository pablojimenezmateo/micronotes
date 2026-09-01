#include "app/RightPanel.h"

#include "app/Notes.h"
#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Outline.h"
#include "ui/Theme.h"

#include <algorithm>
#include <string>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::theme;

constexpr float kHeaderHeight = 34.0f;
constexpr float kRowHeight = 24.0f;
constexpr float kPadX = 14.0f;
constexpr float kIndentStep = 12.0f;
constexpr float kBacklinkHeight = 44.0f;

struct PanelRow {
  Rect rect;
  std::size_t entry = 0;
};

// The tab a click lands on, and where each one is drawn. One geometry, read by
// both the paint and the hit test, so a tab cannot be painted off its own
// target.
Rect tabRect(Rect rect, int index, int count) {
  const float width = (rect.w - kPadX * 2.0f) / static_cast<float>(std::max(count, 1));
  return {rect.x + kPadX + width * static_cast<float>(index), rect.y + 6.0f, width, kHeaderHeight - 12.0f};
}

const char* viewLabel(ui::RightPanelView view) {
  switch(view) {
    case ui::RightPanelView::Outline: return "Outline";
    case ui::RightPanelView::Backlinks: return "Links";
    case ui::RightPanelView::Tags: return "Tags";
  }
  return "";
}

constexpr ui::RightPanelView kViews[] = {ui::RightPanelView::Outline, ui::RightPanelView::Backlinks,
                                        ui::RightPanelView::Tags};

std::vector<PanelRow> outlineRows(const std::vector<ui::OutlineEntry>& entries, Rect rect) {
  std::vector<PanelRow> rows;
  float y = rect.y + kHeaderHeight + 4.0f;
  for(std::size_t i = 0; i < entries.size(); ++i) {
    rows.push_back({{rect.x, y, rect.w, kRowHeight}, i});
    y += kRowHeight;
  }
  return rows;
}

}

void drawRightPanel(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui::fill(renderer, rect, theme().sidebarBg);
  ui::fill(renderer, {rect.x, rect.y, 1.0f, rect.h}, theme().hairline);
  ui::ClipGuard clip(renderer, rect);

  const auto& workspace = ui.state.workspace();
  const ui::TextStyle tabStyle {ui::FontFamily::Sans, false, false, ui::type().small};
  const ui::TextStyle rowStyle {ui::FontFamily::Sans, false, false, ui::type().ui};

  const int tabCount = static_cast<int>(std::size(kViews));
  for(int i = 0; i < tabCount; ++i) {
    const Rect tab = tabRect(rect, i, tabCount);
    const bool active = workspace.rightPanelView == kViews[i];
    const bool hot = ui::contains(tab, ui.mouseX, ui.mouseY);
    if(active) ui::fill(renderer, tab, theme().selectedBg);
    else if(hot) ui::fill(renderer, tab, theme().hoverBg);
    const auto label = viewLabel(kViews[i]);
    const float labelX = tab.x + (tab.w - static_cast<float>(text.width(label, tabStyle))) / 2.0f;
    text.draw(label, labelX, tab.y + 4.0f, active ? theme().text : theme().dim, tabStyle);
  }
  ui::hLine(renderer, rect.x, rect.x + rect.w, rect.y + kHeaderHeight - 1.0f, theme().hairline);

  if(ui.state.selection().noteId.empty()) {
    ui::drawEmptyMessage(text, "Nothing open", "Open a note to see what is in it.",
                         {rect.x, rect.y + kHeaderHeight, rect.w, 100.0f});
    return;
  }

  if(workspace.rightPanelView == ui::RightPanelView::Outline) {
    const auto entries = ui::outlineOf(ui.editor.text());
    if(entries.empty()) {
      ui::drawEmptyMessage(text, "No headings", "Headings in this note show up here.",
                           {rect.x, rect.y + kHeaderHeight, rect.w, 100.0f});
      return;
    }
    const auto current = ui::outlineEntryAt(entries, ui.editor.cursor());
    for(const auto& row : outlineRows(entries, rect)) {
      if(row.rect.y > rect.y + rect.h) break;
      const auto& entry = entries[row.entry];
      const bool here = row.entry == current;
      const bool hot = ui::contains(row.rect, ui.mouseX, ui.mouseY);
      ui::drawSelection(renderer, row.rect, here, hot);
      const float x = rect.x + kPadX + static_cast<float>(entry.depth) * kIndentStep;
      // A top-level heading carries the note's structure and reads as the
      // strong row; anything nested under it is support.
      const auto colour = here ? theme().text : (entry.depth == 0 ? theme().muted : theme().dim);
      text.draw(ui::ellipsizeToWidth(text, entry.text, static_cast<int>(rect.x + rect.w - x - kPadX), rowStyle),
                x, row.rect.y + 3.0f, colour, rowStyle);
    }
    return;
  }

  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    const auto backlinks = ui.state.backlinksToSelected();
    if(backlinks.empty()) {
      ui::drawEmptyMessage(text, "Nothing links here",
                           "Write [[the title of this note]] in another note and it will show up.",
                           {rect.x, rect.y + kHeaderHeight, rect.w, 110.0f});
      return;
    }
    const ui::TextStyle lineStyle {ui::FontFamily::Sans, false, false, ui::type().small};
    float y = rect.y + kHeaderHeight + 4.0f;
    ui.backlinkRows.clear();
    for(const auto& link : backlinks) {
      if(y > rect.y + rect.h) break;
      const Rect row {rect.x, y, rect.w, kBacklinkHeight};
      ui::drawSelection(renderer, row, false, ui::contains(row, ui.mouseX, ui.mouseY));
      const int room = static_cast<int>(rect.w - kPadX * 2.0f);
      text.draw(ui::ellipsizeToWidth(text, link.title, room, rowStyle),
                rect.x + kPadX, y + 2.0f, theme().text, rowStyle);
      // The line the link was written on, which is the whole difference between
      // a list of titles and a reason to click one.
      text.draw(ui::ellipsizeToWidth(text, link.line, room, lineStyle),
                rect.x + kPadX, y + 22.0f, theme().dim, lineStyle);
      ui.backlinkRows.push_back({row, link.id});
      y += kBacklinkHeight;
    }
    return;
  }

  const auto note = ui.state.selectedNote();
  const auto& tags = note ? note->metadata.tags : std::vector<std::string>();
  if(tags.empty()) {
    ui::drawEmptyMessage(text, "No tags", "This note carries none yet.",
                         {rect.x, rect.y + kHeaderHeight, rect.w, 100.0f},
                         ui::keysFor(ui::ActionId::EditTags) + "  edit tags");
    return;
  }
  float y = rect.y + kHeaderHeight + 8.0f;
  for(const auto& tag : tags) {
    const std::string label = "#" + tag;
    const Rect chip {rect.x + kPadX, y, static_cast<float>(text.width(label, rowStyle)) + 16.0f, 22.0f};
    ui::drawSurface(renderer, chip, theme().chipBg, theme().accentDim);
    text.draw(label, chip.x + 8.0f, y + 2.0f, theme().accent, rowStyle);
    y += 28.0f;
  }
}

bool handleRightPanelClick(UiRuntime& ui, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  auto& workspace = ui.state.workspace();
  const int tabCount = static_cast<int>(std::size(kViews));
  for(int i = 0; i < tabCount; ++i) {
    if(!ui::contains(tabRect(rect, i, tabCount), x, y)) continue;
    workspace.rightPanelView = kViews[i];
    return true;
  }
  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    for(const auto& row : ui.backlinkRows) {
      if(!ui::contains(row.rect, x, y)) continue;
      selectNoteById(ui, row.noteId);
      return true;
    }
    return true;
  }
  if(workspace.rightPanelView != ui::RightPanelView::Outline) return true;
  const auto entries = ui::outlineOf(ui.editor.text());
  for(const auto& row : outlineRows(entries, rect)) {
    if(!ui::contains(row.rect, x, y)) continue;
    // Clicking a heading is a way of scrolling to it, so the caret goes to its
    // text rather than to the marker in front of it.
    ui.editor.moveCursor(entries[row.entry].offset);
    ui.revealEditorCursor = true;
    ui.focus = FocusArea::Editor;
    return true;
  }
  // The panel swallows clicks that land on its own background, or a click meant
  // for a row would fall through to the page behind it.
  return true;
}


// Showing and hiding a panel. The status line names what moved, because the
// window can be rearranged by a key that gives no other sign it did anything.
void togglePanel(UiRuntime& ui, bool ui::WorkspaceModel::*panel, std::string_view name) {
  auto& workspace = ui.state.workspace();
  const bool showing = !(workspace.*panel);
  if(!workspace.togglePanel(panel)) {
    ui.status = "The last panel stays open";
    return;
  }
  ui.status = std::string(name) + (showing ? " shown" : " hidden");
}

void cycleRightPanel(UiRuntime& ui) {
  auto& workspace = ui.state.workspace();
  switch(workspace.rightPanelView) {
    case ui::RightPanelView::Outline: workspace.rightPanelView = ui::RightPanelView::Backlinks; break;
    case ui::RightPanelView::Backlinks: workspace.rightPanelView = ui::RightPanelView::Tags; break;
    case ui::RightPanelView::Tags: workspace.rightPanelView = ui::RightPanelView::Outline; break;
  }
  // Cycling what the panel shows is also a way of asking for it.
  workspace.rightPanelVisible = true;
  ui.status = std::string(ui::rightPanelViewName(workspace.rightPanelView));
}

}
