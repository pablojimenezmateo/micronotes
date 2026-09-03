#include "app/RightPanel.h"

#include "app/Notes.h"
#include "app/Shell.h"

#include "core/perf/PerformanceCounters.h"

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

// The rows are a fixed pitch from the top of the panel, so the row for entry
// `i` is arithmetic. It used to be a `vector<PanelRow>` built for every heading
// in the note and then walked until the first one fell off the bottom -- an
// allocation per frame to address the twenty rows a panel can show.
Rect outlineRowRect(Rect rect, std::size_t index) {
  return {rect.x, rect.y + kHeaderHeight + 4.0f + static_cast<float>(index) * kRowHeight, rect.w,
          kRowHeight};
}

// The outline of the open buffer, rebuilt only when the buffer has moved.
const std::vector<ui::OutlineEntry>& outlineFor(UiRuntime& ui) {
  auto& memo = ui.rightPanel;
  const std::uint64_t revision = ui.editor.revision();
  if(memo.outlineValid && memo.outlineRevision == revision) {
    perf::addCounter(perf::CounterId::RightPanelOutlineReused);
    return memo.outline;
  }
  perf::addCounter(perf::CounterId::RightPanelOutlineBuilds);
  memo.outlineValid = true;
  memo.outlineRevision = revision;
  memo.outline = ui::outlineOf(ui.editor.text());
  return memo.outline;
}

// The two views that come from the library rather than from the buffer. Both are
// filled together because both turn on the same key, and asking for either is
// what says the note or the library has moved.
void refreshLibraryViews(UiRuntime& ui) {
  auto& memo = ui.rightPanel;
  const std::string& noteId = ui.state.selection().noteId;
  const std::uint64_t revision = ui.state.revision();
  if(memo.libraryValid && memo.noteId == noteId && memo.libraryRevision == revision) {
    perf::addCounter(perf::CounterId::RightPanelLibraryReused);
    return;
  }
  perf::addCounter(perf::CounterId::RightPanelLibraryBuilds);
  memo.libraryValid = true;
  memo.noteId = noteId;
  memo.libraryRevision = revision;
  memo.backlinks.clear();
  memo.tags.clear();
  if(noteId.empty() || !ui.state.hasLibrary()) return;
  memo.backlinks = ui.state.backlinksToSelected();
  // Only the tags are kept, not the whole note: `selectedNote` reads the file
  // and hands back its body as well, and holding that here would be a second
  // copy of the open buffer for the sake of a row of chips.
  if(const auto note = ui.state.selectedNote()) memo.tags = note->metadata.tags;
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
    if(active) ui::fillRounded(renderer, tab, theme().selectedBg, ui::kRadiusSmall);
    else if(hot) ui::fillRounded(renderer, tab, theme().hoverBg, ui::kRadiusSmall);
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
    const auto& entries = outlineFor(ui);
    if(entries.empty()) {
      ui::drawEmptyMessage(text, "No headings", "Headings in this note show up here.",
                           {rect.x, rect.y + kHeaderHeight, rect.w, 100.0f});
      return;
    }
    // Which entry the caret is in is not memoised: it moves with the caret
    // rather than with the buffer, and it is a walk of the headings rather than
    // of the note.
    const auto current = ui::outlineEntryAt(entries, ui.editor.cursor());
    for(std::size_t i = 0; i < entries.size(); ++i) {
      const Rect row = outlineRowRect(rect, i);
      if(row.y > rect.y + rect.h) break;
      const auto& entry = entries[i];
      const bool here = i == current;
      const bool hot = ui::contains(row, ui.mouseX, ui.mouseY);
      ui::drawSelection(renderer, row, here, hot);
      const float x = rect.x + kPadX + static_cast<float>(entry.depth) * kIndentStep;
      // A top-level heading carries the note's structure and reads as the
      // strong row; anything nested under it is support.
      const auto colour = here ? theme().text : (entry.depth == 0 ? theme().muted : theme().dim);
      text.draw(ui::ellipsizeToWidth(text, entry.text, static_cast<int>(rect.x + rect.w - x - kPadX), rowStyle),
                x, row.y + 3.0f, colour, rowStyle);
    }
    return;
  }

  refreshLibraryViews(ui);
  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    const auto& backlinks = ui.rightPanel.backlinks;
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

  const auto& tags = ui.rightPanel.tags;
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
  const auto& entries = outlineFor(ui);
  // The rows are a fixed pitch, so the one under the pointer is arithmetic
  // rather than a walk of every heading in the note.
  const float offset = y - (rect.y + kHeaderHeight + 4.0f);
  if(offset >= 0.0f) {
    const auto index = static_cast<std::size_t>(offset / kRowHeight);
    if(index < entries.size() && ui::contains(outlineRowRect(rect, index), x, y)) {
      // Clicking a heading is a way of scrolling to it, so the caret goes to its
      // text rather than to the marker in front of it.
      ui.editor.moveCursor(entries[index].offset);
      ui.revealEditorCursor = true;
      ui.focus = FocusArea::Editor;
      return true;
    }
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
