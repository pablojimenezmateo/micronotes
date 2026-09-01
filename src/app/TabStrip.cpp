#include "app/TabStrip.h"

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

std::vector<ui::TabSlot> slotsFor(const UiRuntime& ui, ui::TextRenderer* text, Rect rect) {
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().ui};
  std::function<int(std::string_view)> measure;
  if(text) {
    measure = [text, style](std::string_view value) {
      return text->width(value, style);
    };
  }
  return ui::layoutTabs(tabTitles(ui), rect, measure);
}

// Drawn rather than typeset: the UI face has no glyph for a close cross that
// stays crisp at this size, and a missing glyph here would read as a bug.
void drawCross(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float inset = 4.0f;
  const float x0 = box.x + inset;
  const float y0 = box.y + inset;
  const float x1 = box.x + box.w - inset;
  const float y1 = box.y + box.h - inset;
  SDL_RenderLine(renderer, x0, y0, x1, y1);
  SDL_RenderLine(renderer, x0, y1, x1, y0);
}

}

void drawTabStrip(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui::fill(renderer, rect, theme().appBg);
  ui::ClipGuard clip(renderer, rect);
  const auto& workspace = ui.state.workspace();
  const auto titles = tabTitles(ui);
  const auto slots = slotsFor(ui, &text, rect);
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().ui};

  for(const auto& slot : slots) {
    if(!slot.visible) break;
    const bool active = slot.index == workspace.activeTab;
    const bool hot = ui::contains(slot.rect, ui.mouseX, ui.mouseY);
    if(active) ui::fill(renderer, slot.rect, theme().editorBg);
    else if(hot) ui::fill(renderer, slot.rect, theme().hoverBg);
    // The active tab joins the page below it, so its accent sits on top and its
    // bottom edge is the only one without a rule.
    if(active) ui::fill(renderer, {slot.rect.x, slot.rect.y, slot.rect.w, 2.0f}, theme().accent);
    ui::fill(renderer, {slot.rect.x + slot.rect.w - 1.0f, slot.rect.y + 6.0f, 1.0f, slot.rect.h - 12.0f},
             theme().hairline);

    const float textLeft = slot.rect.x + ui::kTabClosePadding + 4.0f;
    const int room = static_cast<int>(slot.close.x - textLeft - 4.0f);
    text.draw(ui::ellipsizeToWidth(text, titles[slot.index], room, style), textLeft, slot.rect.y + 7.0f,
              active ? theme().text : theme().muted, style);
    // The close button appears on the tab you are pointing at and on the one
    // you are reading; a strip of crosses is a strip that reads as a warning.
    if(active || hot) {
      const bool overClose = ui::contains(ui::tabCloseHitRect(slot), ui.mouseX, ui.mouseY);
      drawCross(renderer, slot.close, overClose ? theme().text : theme().dim);
    }
  }
  ui::hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().hairline);
}

bool handleTabStripClick(UiRuntime& ui, Rect rect, float x, float y, Uint8 button, bool ctrl) {
  if(!ui::contains(rect, x, y)) return false;
  // Laid out with no measurer: the geometry is a pure function of the titles
  // and the strip, and the close targets do not depend on the font.
  const auto slots = slotsFor(ui, nullptr, rect);
  for(const auto& slot : slots) {
    if(!slot.visible || !ui::contains(slot.rect, x, y)) continue;
    // Middle click closes, as it does in every tab strip; so does the cross.
    if(button == SDL_BUTTON_MIDDLE || ui::contains(ui::tabCloseHitRect(slot), x, y)) {
      if(!saveCurrent(ui, true)) return true;
      ui.state.closeTab(slot.index);
      loadSelectedIntoEditor(ui);
      return true;
    }
    if(button != SDL_BUTTON_LEFT) return true;
    if(ctrl) {
      // Ctrl+click pins, which is how a tab stops being the one that gets
      // replaced by the next note opened.
      auto& tabs = ui.state.workspace().tabs;
      if(slot.index < tabs.size()) tabs[slot.index].pinned = !tabs[slot.index].pinned;
      return true;
    }
    if(slot.index == ui.state.workspace().activeTab) return true;
    if(!saveCurrent(ui, true)) return true;
    ui.state.workspace().activeTab = slot.index;
    ui.state.selectNote(ui.state.workspace().tabs[slot.index].noteId);
    loadSelectedIntoEditor(ui);
    return true;
  }
  return true;
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
