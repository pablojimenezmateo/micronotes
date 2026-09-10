#include "app/TabStrip.h"

#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Tabs.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/ClipGuard.h"
#include "core/render/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
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
    const auto note = ui.state.catalog().findNote(tab.noteId);
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

// The two things a drag adds to the strip: where the tab would land, and the
// tab itself under the pointer.
//
// Drawn after everything else, so the carried tab passes over its neighbours
// rather than under them -- a ghost that slides behind the strip reads as the
// tab having been dropped already.
void drawTabDrag(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect strip,
                 const ui::StripTabColors& colors) {
  const TabDrag& drag = ui.tabStrip.drag;
  const auto& layout = ui.tabStrip.layout;
  const auto& titles = ui.tabStrip.titles;
  if(drag.source >= titles.size()) return;

  // The insertion caret: a full-height accent rule in the gap the tab would
  // take. At the leading edge of the tab now in that slot, or the trailing edge
  // of the last one when the drop is past the end.
  float caretX = strip.x;
  bool haveCaret = false;
  for(const auto& slot : layout.slots) {
    if(!slot.visible) continue;
    if(slot.index == drag.dropSlot) {
      caretX = slot.rect.x;
      haveCaret = true;
      break;
    }
    caretX = slot.rect.x + slot.rect.w;
    haveCaret = true;
  }
  if(haveCaret) {
    ui::fill(renderer, {std::round(caretX) - ui::kTabDropCaretWidth / 2.0f, strip.y,
                        ui::kTabDropCaretWidth, strip.h},
             theme().accent);
  }

  // The carried tab. Drawn as the active one whether or not it is -- it is the
  // one thing the pointer is holding -- with a shadow behind it and an accent
  // outline, which together are what lift it off the strip.
  const float x = ui::draggedTabX(strip, drag.tabWidth, drag.pointerX, drag.grabOffsetX);
  const Rect carried {x, strip.y, drag.tabWidth, strip.h};
  ui::fill(renderer, {carried.x + 1.0f, carried.y + 2.0f, carried.w, carried.h},
           render::blend(theme().surfaceBackground, SDL_Color{0, 0, 0, 255}, 0.5f));
  ui::drawStripTab(renderer, text, carried, titles[drag.source], true, false,
                   ui::kTabCloseReserve, colors);
  ui::stroke(renderer, carried, theme().accent);
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

  const TabDrag& drag = ui.tabStrip.drag;

  for(const auto& slot : layout.slots) {
    // `continue`, not `break`. Under the old layout the only invisible tabs
    // were the ones past the right edge, so stopping at the first was the same
    // thing; a strip that scrolls is invisible at *both* ends, and breaking on
    // the tab scrolled off the left drew nothing at all from the first time the
    // strip overflowed. The hit test beside it already used `continue`, so the
    // two disagreed about the same list.
    if(!slot.visible) continue;
    // The tab being carried is lifted out of the strip for the whole gesture
    // and drawn last, following the pointer. The hole it leaves stays open,
    // which is what says the strip is short one tab rather than that the order
    // has already changed.
    if(drag.dragging && slot.index == drag.source) continue;
    const bool active = slot.index == workspace.activeTab;
    const bool hot = ui.pointer.over(slot.rect);
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
    // Never during a drag: the pointer is holding a tab, not aiming at a cross,
    // and the release would land on one it passed over.
    if(drag.dragging || (!active && !hot)) continue;
    const bool overClose = ui.pointer.over(ui::tabCloseHitRect(slot));
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

  if(drag.dragging) drawTabDrag(renderer, text, ui, rect, colors);
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
      if(!leaveOpenNote(ui, true)) return true;
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
    // A left press on a tab is both a switch and the start of a possible drag.
    // Armed before the switch, and against the slot rather than the active tab,
    // so a drag still works on the tab that was already showing and on one
    // whose switch a failed save refused.
    ui.tabStrip.drag = TabDrag {
      .pressed = true,
      .pressX = x,
      .pressY = y,
      .source = slot.index,
      .grabOffsetX = x - slot.rect.x,
      .tabWidth = slot.rect.w,
      .pointerX = x,
      // Where it would land if it never moved, which is where it already is.
      .dropSlot = slot.index,
    };
    if(slot.index == ui.state.workspace().activeTab) return true;
    if(!leaveOpenNote(ui, true)) return true;
    ui.state.editWorkspace().activeTab = slot.index;
    ui.state.selectNote(ui.state.workspace().tabs[slot.index].noteId);
    loadSelectedIntoEditor(ui);
    return true;
  }
  return true;
}

bool handleTabStripMotion(UiRuntime& ui, float x, float y) {
  auto& drag = ui.tabStrip.drag;
  if(!drag.pressed) return false;
  // The button let go without a release reaching us -- a window that lost focus
  // mid-gesture is the usual way. Commit what the drag had resolved to and hand
  // the motion on, rather than leaving a drag armed against a pointer that is
  // no longer holding anything.
  if((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0) {
    handleTabStripRelease(ui);
    return false;
  }
  if(!drag.dragging) {
    const float dx = x - drag.pressX;
    const float dy = y - drag.pressY;
    if(std::hypot(dx, dy) < ui::kTabDragStartDistance) return true;
    drag.dragging = true;
  }
  drag.pointerX = x;
  const std::size_t count = ui.state.workspace().tabs.size();
  // From the drawn tab's leading edge, not from the pointer -- see `tabDropSlot`.
  const float carriedX =
    ui::draggedTabX(ui.tabStrip.rect, drag.tabWidth, x, drag.grabOffsetX);
  drag.dropSlot = ui::tabDropSlot(ui.tabStrip.layout, ui.tabStrip.rect, carriedX, count);
  return true;
}

void handleTabStripRelease(UiRuntime& ui) {
  const TabDrag drag = ui.tabStrip.drag;
  ui.tabStrip.drag.clear();
  if(!drag.dragging) return;
  auto& workspace = ui.state.editWorkspace();
  const std::size_t count = workspace.tabs.size();
  const std::size_t target = ui::tabIndexForDropSlot(drag.dropSlot, drag.source, count);
  if(target == drag.source) return;
  if(!ui::moveTab(workspace.tabs, workspace.activeTab, drag.source, target)) return;
  // No reload: the strip is a different order and the note showing is the same
  // note. Only the selection could go stale, and `activeTab` was carried with
  // the tab it named, so it has not.
  ui.status = "Moved tab";
}


bool handleTabMenuResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId != "tab-menu") return false;
  auto& workspace = ui.state.editWorkspace();
  const auto index = workspace.findTab(result.value);
  if(result.itemId == "close") {
    if(index == std::string::npos) return true;
    if(!leaveOpenNote(ui, true)) return true;
    ui.state.closeTab(index);
    loadSelectedIntoEditor(ui);
    return true;
  }
  // The four bulk closes, each about the tab the menu was opened on rather than
  // the one showing -- which is the whole reason a right click does not switch
  // to a tab, and why "close to the right" means right of *that* tab.
  static constexpr std::pair<std::string_view, TabCloseScope> kScopes[] {
    {"close-others", TabCloseScope::Others},
    {"close-right", TabCloseScope::ToRight},
    {"close-left", TabCloseScope::ToLeft},
    {"close-all", TabCloseScope::All},
  };
  for(const auto& [itemId, scope] : kScopes) {
    if(result.itemId != itemId) continue;
    if(index != std::string::npos) closeTabs(ui, index, scope);
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

// Moving between tabs and closing them. Both put the note that is open away
// first, so switching can never lose an edit -- and only when there is
// something to put away, so a note whose file has gone is not a tab you are
// stuck in. See `leaveOpenNote`.
void stepTab(UiRuntime& ui, int delta) {
  if(ui.state.workspace().tabs.size() < 2) return;
  if(!leaveOpenNote(ui, true)) return;
  ui.state.stepTab(delta);
  loadSelectedIntoEditor(ui);
}

void closeActiveTab(UiRuntime& ui) {
  const auto& workspace = ui.state.workspace();
  if(workspace.tabs.empty()) return;
  if(!leaveOpenNote(ui, true)) return;
  ui.state.closeTab(workspace.activeTab);
  loadSelectedIntoEditor(ui);
}

std::size_t closeTabs(UiRuntime& ui, std::size_t index, TabCloseScope scope) {
  const auto& tabs = ui.state.workspace().tabs;
  if(index >= tabs.size()) return 0;
  std::vector<std::size_t> doomed;
  for(std::size_t i = 0; i < tabs.size(); ++i) {
    // Pinned tabs are spared, in all four scopes. Pinning already means "this
    // one stays" -- it is the tab the ceiling will not evict and a browse will
    // not take over -- so a reader who pinned a note has already said the one
    // thing there is to say about it, and "Close all tabs" taking it anyway
    // would make the pin mean nothing the moment it mattered most.
    if(tabs[i].pinned) continue;
    const bool inScope = scope == TabCloseScope::All       ? true
                         : scope == TabCloseScope::Others  ? i != index
                         : scope == TabCloseScope::ToRight ? i > index
                                                           : i < index;
    if(inScope) doomed.push_back(i);
  }
  if(doomed.empty()) {
    ui.status = "No tabs to close";
    return 0;
  }
  if(!leaveOpenNote(ui, true)) return 0;
  // Highest index first. Closing a tab renumbers every tab after it, so a
  // forward walk closes the wrong note from its second step on -- and the last
  // steps run off the end of a strip that has since shrunk.
  for(auto at = doomed.rbegin(); at != doomed.rend(); ++at) ui.state.closeTab(*at);
  loadSelectedIntoEditor(ui);
  ui.status = "Closed " + std::to_string(doomed.size()) + (doomed.size() == 1 ? " tab" : " tabs");
  return doomed.size();
}

void closeTabsAroundActive(UiRuntime& ui, TabCloseScope scope) {
  closeTabs(ui, ui.state.workspace().activeTab, scope);
}

}
