#include "app/TabStrip.h"

#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Tabs.h"
#include "ui/Theme.h"

#include <string>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::theme;

// The titles the strip shows, in tab order. A tab whose note has gone missing
// keeps its place and says so, rather than silently disappearing and taking
// whatever was beside it one step to the left.
std::vector<std::string> tabTitles(const UiRuntime& ui) {
  std::vector<std::string> titles;
  for(const auto& tab : ui.state.workspace().tabs) {
    const auto note = ui.state.findNote(tab.noteId);
    titles.push_back(note ? note->title : "Missing note");
  }
  return titles;
}

// The strip's own text measurer. The widths depend on it, which is why the
// layout is built where the renderer is and read everywhere else.
std::function<int(std::string_view)> tabMeasure(ui::TextRenderer& text) {
  const ui::TextStyle style = ui::chromeStyle();
  return [&text, style](std::string_view value) { return text.width(value, style); };
}

}
void drawTabStrip(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rect) {
  // The strip is chrome, so it takes the chrome's ground; the active tab is a
  // step up out of it with a 2px accent lid. The lid is what makes that tab
  // read as continuous with the note under it, and it is why the strip needs no
  // seam between one tab and its active neighbour.
  ui::fill(renderer, rect, theme().chromeBackground);
  ui::ClipGuard clip(renderer, rect);
  const auto& workspace = ui.state.workspace();
  ui.tabStrip.rect = rect;
  ui.tabStrip.titles = tabTitles(ui);
  ui.tabStrip.layout = ui::layoutTabs(ui.tabStrip.titles, rect, tabMeasure(text), workspace.activeTab);
  const auto& titles = ui.tabStrip.titles;
  const auto& layout = ui.tabStrip.layout;
  const ui::StripTabColors colors = ui::stripTabColors();

  for(const auto& slot : layout.slots) {
    // `continue`, not `break`. Under the old layout the only invisible tabs
    // were the ones past the right edge, so stopping at the first was the same
    // thing; a strip that scrolls is invisible at *both* ends, and breaking on
    // the tab scrolled off the left drew nothing at all from the first time the
    // strip overflowed. The hit test beside it already used `continue`, so the
    // two disagreed about the same list -- see TD-20.
    if(!slot.visible) continue;
    const bool active = slot.index == workspace.activeTab;
    const bool hot = ui::contains(slot.rect, ui.pointer.x, ui.pointer.y);
    ui::drawStripTab(renderer, text, slot.rect, titles[slot.index], active, hot,
                     ui::kTabCloseReserve, colors);
    // A tab only says what it is when the title did not fit. Repeating a title
    // that is already legible is noise.
    const ui::TextStyle style = ui::chromeStyle();
    const float room = slot.rect.w - ui::kSidebarInset - ui::kTabCloseReserve;
    if(static_cast<float>(text.width(titles[slot.index], style)) > room) {
      ui.pointer.offerTooltip(slot.rect, titles[slot.index]);
    }
    // The close button appears on the tab you are pointing at and on the one
    // you are reading; a strip of crosses is a strip that reads as a warning.
    if(!active && !hot) continue;
    const bool overClose = ui::contains(ui::tabCloseHitRect(slot), ui.pointer.x, ui.pointer.y);
    ui::drawCloseGlyph(renderer, slot.close,
                       overClose ? theme().textPrimary
                       : active  ? colors.activeText
                                 : colors.inactiveText);
    // Offered after the tab's own, so the innermost control wins.
    ui.pointer.offerTooltip(ui::tabCloseHitRect(slot), "Close " + titles[slot.index]);
  }

  // The overflow chevrons last, over whichever tab reaches under them.
  ui::drawStripOverflowButton(renderer, text, layout.scrollLeft, false, layout.hiddenLeft,
                              ui.pointer.over(layout.scrollLeft));
  ui::drawStripOverflowButton(renderer, text, layout.scrollRight, true, layout.hiddenRight,
                              ui.pointer.over(layout.scrollRight));
  if(layout.hiddenLeft > 0) ui.pointer.offerTooltip(layout.scrollLeft, "Earlier tabs");
  if(layout.hiddenRight > 0) ui.pointer.offerTooltip(layout.scrollRight, "Later tabs");
}

bool tabStripHasControlAt(const UiRuntime& ui, float x, float y) {
  if(!ui::contains(ui.tabStrip.rect, x, y)) return false;
  const auto& layout = ui.tabStrip.layout;
  // A chevron with nothing hidden behind it is drawn dimmed and does nothing,
  // so it is not a control and must not claim the pointer.
  if(layout.hiddenLeft > 0 && ui::contains(layout.scrollLeft, x, y)) return true;
  if(layout.hiddenRight > 0 && ui::contains(layout.scrollRight, x, y)) return true;
  for(const auto& slot : layout.slots) {
    if(slot.visible && ui::contains(slot.rect, x, y)) return true;
  }
  return false;
}

bool handleTabStripClick(UiRuntime& ui, float x, float y, Uint8 button, bool ctrl) {
  if(!ui::contains(ui.tabStrip.rect, x, y)) return false;
  const auto& layout = ui.tabStrip.layout;
  // The chevrons first: they are drawn over the tabs that reach under them, so
  // they are clicked before them too. Each steps the active tab one along,
  // which is what scrolls the derived window -- there is no scroll to set.
  if(ui::contains(layout.scrollLeft, x, y)) {
    if(layout.hiddenLeft > 0 && button == SDL_BUTTON_LEFT) stepTab(ui, -1);
    return true;
  }
  if(ui::contains(layout.scrollRight, x, y)) {
    if(layout.hiddenRight > 0 && button == SDL_BUTTON_LEFT) stepTab(ui, 1);
    return true;
  }
  for(const auto& slot : layout.slots) {
    if(!slot.visible || !ui::contains(slot.rect, x, y)) continue;
    // Middle click closes, as it does in every tab strip; so does the cross.
    if(button == SDL_BUTTON_MIDDLE || ui::contains(ui::tabCloseHitRect(slot), x, y)) {
      if(!saveCurrent(ui, true)) return true;
      ui.state.closeTab(slot.index);
      loadSelectedIntoEditor(ui);
      return true;
    }
    // A right click is about *this* tab and does not switch to it: a menu that
    // moved the reader somewhere before they had chosen anything would be
    // acting before it was asked to.
    if(button == SDL_BUTTON_RIGHT) {
      const auto& tabs = ui.state.workspace().tabs;
      if(slot.index < tabs.size()) openTabMenu(ui, tabs[slot.index].noteId, x, y);
      return true;
    }
    if(button != SDL_BUTTON_LEFT) return true;
    if(ctrl) {
      // Ctrl+click pins, which is how a tab stops being the one that gets
      // replaced by the next note opened.
      auto& tabs = ui.state.editWorkspace().tabs;
      if(slot.index < tabs.size()) tabs[slot.index].pinned = !tabs[slot.index].pinned;
      return true;
    }
    if(slot.index == ui.state.workspace().activeTab) return true;
    if(!saveCurrent(ui, true)) return true;
    ui.state.editWorkspace().activeTab = slot.index;
    ui.state.selectNote(ui.state.workspace().tabs[slot.index].noteId);
    loadSelectedIntoEditor(ui);
    return true;
  }
  return true;
}


bool handleTabMenuResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId != "tab-menu") return false;
  auto& workspace = ui.state.editWorkspace();
  const auto index = workspace.findTab(result.value);
  if(result.itemId == "close") {
    if(index == std::string::npos) return true;
    if(!saveCurrent(ui, true)) return true;
    ui.state.closeTab(index);
    loadSelectedIntoEditor(ui);
    return true;
  }
  if(result.itemId == "pin") {
    if(index != std::string::npos) workspace.tabs[index].pinned = !workspace.tabs[index].pinned;
    ui.status = index != std::string::npos && workspace.tabs[index].pinned ? "Pinned tab"
                                                                           : "Unpinned tab";
    return true;
  }
  // The three path commands, about this tab's note rather than the open one.
  return handleNotePathCommand(ui, result.itemId, result.value);
}

// Moving between tabs and closing them. Both write the note that is open
// first, so switching can never lose an edit.
void stepTab(UiRuntime& ui, int delta) {
  if(ui.state.workspace().tabs.size() < 2) return;
  if(!saveCurrent(ui, true)) return;
  ui.state.stepTab(delta);
  loadSelectedIntoEditor(ui);
}

void closeActiveTab(UiRuntime& ui) {
  const auto& workspace = ui.state.workspace();
  if(workspace.tabs.empty()) return;
  if(!saveCurrent(ui, true)) return;
  ui.state.closeTab(workspace.activeTab);
  loadSelectedIntoEditor(ui);
}

}
