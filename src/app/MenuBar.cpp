#include "app/MenuBar.h"

#include "app/Shell.h"

#include "core/AppIdentity.h"

#include "ui/Metrics.h"

#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace micronotes::app {
namespace {

using ui::MenuBarLayout;
using ui::MenuId;
using ui::MenuItemSpec;
using ui::Rect;
using ui::TextRenderer;
using ui::drawChevron;
using ui::drawMenuRow;
using ui::drawWindowGlyph;
using ui::fill;
using ui::stroke;
using ui::theme;

// The bar's geometry, through the face it is actually drawn in.
//
// Not a convenience: the item widths depend on the measurer, so a caller that
// omits one lays out a *different* bar from the one that gets painted -- which
// is the bug the tab strip had for a release, where a click on the second tab
// activated the first.
MenuBarLayout layoutFor(TextRenderer& text, const UiRuntime& ui, Rect rect) {
  const ui::TextStyle style = ui::chromeStyle();
  return ui::menuBarLayout(rect, ui.chrome.openMenu, ui.chrome.customChrome,
                           [&text, style](std::string_view value) {
                             return text.width(value, style);
                           });
}

// The items of the menu that is open, and the popup they are drawn in. Empty
// when nothing is open, so every caller can ask unconditionally.
struct OpenMenu {
  std::span<const MenuItemSpec> items;
  Rect popup;
};

OpenMenu openMenuFor(TextRenderer& text, const UiRuntime& ui, Rect rect, Rect bounds) {
  if(ui.chrome.openMenu == MenuId::None) return {};
  const ui::MenuSpec* spec = ui::findMenu(ui.chrome.openMenu);
  if(!spec) return {};

  // The anchor is the bar item when the menu is on the bar, and the chevron
  // when it overflowed into it -- so a menu reached through the chevron drops
  // its popup under the chevron rather than under wherever it would have sat.
  const MenuBarLayout layout = layoutFor(text, ui, rect);
  Rect anchor = layout.chevron;
  for(const auto& item : layout.items) {
    if(item.id == ui.chrome.openMenu) anchor = item.rect;
  }
  if(ui::empty(anchor)) return {};

  const ui::TextStyle style = ui::chromeStyle();
  const Rect popup = ui::menuPopupRect(anchor, spec->items, bounds,
                                       [&text, style](std::string_view value) {
                                         return text.width(value, style);
                                       });
  return {spec->items, popup};
}

// The rule under the letter `Alt` opens this menu by.
//
// Drawn always, not only while `Alt` is held. The bar exists because a shell
// whose only routes to a command are a chord and a palette is one where every
// command has to be learnt before it can be used -- and a key you have to hold
// something down to be told about is a key you are not told about.
//
// Two measurements: the prefix places it, the letter sizes it. That is the
// whole reason a mnemonic is not free -- everything else on this bar measures
// one whole label.
void drawMnemonicUnderline(TextRenderer& text, SDL_Renderer* renderer, const ui::MenuSpec& spec,
                           float labelX, float labelY, SDL_Color ink, const ui::TextStyle& style) {
  const std::size_t at = ui::menuMnemonicIndex(spec);
  if(at == std::string_view::npos) return;
  const auto prefix = spec.label.substr(0, at);
  const auto letter = spec.label.substr(at, 1);
  const float x = labelX + static_cast<float>(text.width(prefix, style));
  const float w = static_cast<float>(text.width(letter, style));
  if(w <= 0.0f) return;
  // One pixel under the baseline box, the width of the letter and no more.
  const float y = std::round(labelY + static_cast<float>(text.lineHeight(style)) - 2.0f);
  fill(renderer, {std::round(x), y, w, 1.0f}, ink);
}

void closeMenu(UiRuntime& ui) {
  ui.chrome.openMenu = MenuId::None;
  ui.chrome.menuHighlight = 0;
}

// The first item a keyboard walk should land on, and the next one in a
// direction. Separators are stepped over rather than landed on, so Down from
// the last item of a group reaches the first of the next.
std::size_t stepHighlight(std::span<const MenuItemSpec> items, std::size_t from, int delta) {
  if(items.empty()) return 0;
  const auto count = static_cast<int>(items.size());
  int at = static_cast<int>(from);
  for(int guard = 0; guard < count; ++guard) {
    at = (at + delta + count) % count;
    if(!items[static_cast<std::size_t>(at)].separator) break;
  }
  return static_cast<std::size_t>(at);
}

}

bool menuItemChecked(const UiRuntime& ui, ui::ActionId action) {
  const auto& workspace = ui.state.workspace();
  switch(action) {
    case ui::ActionId::PaneLive: return workspace.paneMode() == ui::PaneMode::Live;
    case ui::ActionId::PaneRaw: return workspace.paneMode() == ui::PaneMode::Editor;
    case ui::ActionId::PaneReading: return workspace.paneMode() == ui::PaneMode::Viewer;
    case ui::ActionId::PaneSplit: return workspace.paneMode() == ui::PaneMode::Split;
    case ui::ActionId::ToggleSidebar: return workspace.sidebarVisible;
    case ui::ActionId::ToggleRightPanel: return workspace.rightPanelVisible;
    case ui::ActionId::ToggleTheme: return ui::themeMode() == ui::ThemeMode::Light;
    case ui::ActionId::ToggleFavorite: {
      const auto& noteId = ui.state.selection().noteId;
      return !noteId.empty() && ui.state.workspace().isFavorite(noteId);
    }
    case ui::ActionId::PinTab: {
      const auto& tabs = workspace.tabs;
      return workspace.activeTab < tabs.size() && tabs[workspace.activeTab].pinned;
    }
    default: return false;
  }
}

bool menuItemEnabled(const UiRuntime& ui, ui::ActionId action) {
  if(!ui.state.catalog().isOpen()) {
    // With no library open there is nothing to act on but the two things that
    // can get you one, and the two that are always available.
    return action == ui::ActionId::Settings || action == ui::ActionId::Quit ||
           action == ui::ActionId::Shortcuts || action == ui::ActionId::ToggleTheme ||
           action == ui::ActionId::CommandPalette;
  }
  const ui::ActionSpec* spec = ui::findAction(action);
  if(!spec) return false;
  // A few items need something more specific than a note. Listed rather than
  // hidden, for the reason the palette lists them: a menu that changes shape is
  // a menu you cannot learn.
  if(action == ui::ActionId::RenameFolder || action == ui::ActionId::DeleteFolder) {
    return !ui.state.selection().folder.empty();
  }
  if(action == ui::ActionId::MoveBlocks) return ui.blockSelection.active;
  return !spec->needsNote || !ui.state.selection().noteId.empty();
}

void drawMenuBar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui.chrome.windowButtons = {};
  ui.chrome.menuItemsBand = {};
  ui.chrome.menuChevron = {};
  if(ui::empty(rect)) return;
  fill(renderer, rect, theme().chromeBackground);
  ui::hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().border);

  const ui::TextStyle style = ui::chromeStyle();
  const MenuBarLayout layout = layoutFor(text, ui, rect);

  if(!layout.items.empty()) {
    const Rect first = layout.items.front().rect;
    const Rect last = layout.items.back().rect;
    ui.chrome.menuItemsBand = {first.x, first.y, last.x + last.w - first.x, first.h};
  }
  ui.chrome.menuChevron = layout.chevron;

  for(const auto& item : layout.items) {
    const ui::MenuSpec* spec = ui::findMenu(item.id);
    if(!spec) continue;
    const bool hot = !item.active && ui.pointer.over(item.rect);
    if(item.active) {
      fill(renderer, item.rect, theme().chromeActive);
      // A foot rather than a full outline: the popup below overlaps the item's
      // last pixel, and the two together read as one shape.
      fill(renderer, {item.rect.x, item.rect.y + item.rect.h - ui::kRowAccentWidth, item.rect.w,
                      ui::kRowAccentWidth}, theme().accent);
    } else if(hot) {
      fill(renderer, item.rect, theme().rowHighlight);
    }
    const float width = static_cast<float>(text.width(spec->label, style));
    const float labelX = std::round(item.rect.x + (item.rect.w - width) / 2.0f);
    const float labelY = ui::textTop(item.rect, text, style);
    const SDL_Color ink = item.active || hot ? theme().chromeActiveText : theme().chromeText;
    text.draw(spec->label, labelX, labelY, ink, style);
    drawMnemonicUnderline(text, renderer, *spec, labelX, labelY, ink, style);
  }

  if(!ui::empty(layout.chevron)) {
    const bool hot = ui.pointer.over(layout.chevron);
    if(hot) fill(renderer, layout.chevron, theme().rowHighlight);
    // Three dots rather than a chevron: a chevron here would point the same way
    // as every disclosure in the tree and mean something else entirely.
    const float cx = std::round(layout.chevron.x + layout.chevron.w / 2.0f);
    const float cy = std::round(layout.chevron.y + layout.chevron.h / 2.0f);
    const SDL_Color ink = hot ? theme().textPrimary : theme().chromeTextSecondary;
    for(int i = -1; i <= 1; ++i) {
      fill(renderer, {cx - 1.0f, cy + static_cast<float>(i) * 5.0f - 1.0f, 2.0f, 2.0f}, ink);
    }
    ui.pointer.offerTooltip(layout.chevron, "More menus");
  }

  // The application's name, centred, and only when it fits clear of both sides.
  // A title squeezed between two controls it is touching says less than no
  // title, and this is the only thing left that says which window this is.
  if(ui.chrome.customChrome) {
    const std::string title = microcore::kAppName;
    const float width = static_cast<float>(text.width(title, style));
    const float x = std::round(rect.x + (rect.w - width) / 2.0f);
    const float leftLimit = layout.items.empty()
                              ? rect.x + ui::kSpace3
                              : layout.items.back().rect.x + layout.items.back().rect.w + ui::kSpace4;
    const float rightLimit = layout.windowButtons.front().x - ui::kSpace4;
    if(x >= leftLimit && x + width <= rightLimit) {
      text.draw(title, x, ui::textTop(rect, text, style), theme().chromeTextSecondary, style);
    }
  }

  for(std::size_t i = 0; i < layout.windowButtons.size(); ++i) {
    const Rect box = layout.windowButtons[i];
    if(ui::empty(box)) continue;
    ui.chrome.windowButtons[i] = box;
    const bool hot = ui.pointer.over(ui::windowButtonHitRect(box));
    // Close goes red on hover; the other two take the ordinary hover fill.
    if(hot) fill(renderer, box, i == 2 ? theme().warn : theme().rowHighlight);
    const SDL_Color mark = hot ? (i == 2 ? theme().onAccent : theme().textPrimary)
                               : theme().chromeTextSecondary;
    drawWindowGlyph(renderer, box, i, ui.chrome.windowMaximized, mark);
  }
  if(ui.chrome.customChrome) {
    ui.pointer.offerTooltip(ui.chrome.windowButtons[0], "Minimize");
    ui.pointer.offerTooltip(ui.chrome.windowButtons[1], ui.chrome.windowMaximized ? "Restore" : "Maximize");
    ui.pointer.offerTooltip(ui.chrome.windowButtons[2], "Close");
  }
}

void drawOpenMenu(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect bounds) {
  const OpenMenu open = openMenuFor(text, ui, ui.chrome.menuBarRect, bounds);
  if(open.items.empty() || ui::empty(open.popup)) return;

  fill(renderer, open.popup, theme().overlayBackground);
  stroke(renderer, open.popup, theme().border);

  // One cursor for the whole popup, which is the same one `menuPopupItemAt`
  // walks: where a row is has to be one answer, because the paint and the hit
  // test disagreeing is a wrong command run.
  ui::RowCursor rows = ui::menuPopupRows(open.popup);
  for(std::size_t i = 0; i < open.items.size(); ++i) {
    const MenuItemSpec& spec = open.items[i];
    const Rect row = rows.place(ui::menuRowHeight(spec.separator));
    if(spec.separator) {
      ui::drawMenuSeparator(renderer, row);
      continue;
    }
    const std::string accelerator = ui::menuItemAccelerator(spec);
    ui::MenuRow item;
    item.label = ui::menuItemLabel(spec);
    item.accelerator = accelerator;
    item.enabled = menuItemEnabled(ui, spec.action);
    item.highlighted = ui.pointer.over(row) || i == ui.chrome.menuHighlight;
    item.checked = spec.checkable && menuItemChecked(ui, spec.action);
    // Destructive items read as destructive wherever they are offered, which is
    // the same rule the confirm overlays and the context menus follow.
    item.destructive = spec.action == ui::ActionId::DeleteNote ||
                       spec.action == ui::ActionId::DeleteFolder ||
                       spec.action == ui::ActionId::DeleteBlock;
    drawMenuRow(renderer, text, row, item);
  }
}

MenuBarClick handleMenuBarClick(TextRenderer& text, UiRuntime& ui, Rect rect, Rect bounds,
                                float x, float y) {
  MenuBarClick result;

  // The popup first: it is drawn over everything, so it is clicked before
  // everything, including over the bar it hangs from.
  const OpenMenu open = openMenuFor(text, ui, rect, bounds);
  if(!open.items.empty() && ui::contains(open.popup, x, y)) {
    result.handled = true;
    if(const auto index = ui::menuPopupItemAt(open.popup, open.items, x, y)) {
      const MenuItemSpec& spec = open.items[*index];
      closeMenu(ui);
      if(menuItemEnabled(ui, spec.action)) result.action = spec.action;
    }
    return result;
  }

  if(!ui::contains(rect, x, y)) {
    // A press anywhere else dismisses an open menu, and is *not* consumed:
    // clicking a tree row with the File menu open should select that row, which
    // is what every menu bar does.
    if(ui.chrome.openMenu != MenuId::None) closeMenu(ui);
    return result;
  }

  const MenuBarLayout layout = layoutFor(text, ui, rect);
  for(std::size_t i = 0; i < layout.windowButtons.size(); ++i) {
    if(!ui::contains(ui::windowButtonHitRect(layout.windowButtons[i]), x, y)) continue;
    // Left to the window-chrome handler, which owns `pendingWindowAction`: the
    // bar knows where the buttons are, and nothing else about them.
    return result;
  }
  for(const auto& item : layout.items) {
    if(!ui::contains(item.rect, x, y)) continue;
    result.handled = true;
    // A second press on the open menu shuts it, which is how a menu bar lets go
    // of the pointer.
    if(ui.chrome.openMenu == item.id) closeMenu(ui);
    else {
      ui.chrome.openMenu = item.id;
      ui.chrome.menuHighlight = 0;
    }
    return result;
  }
  if(!ui::empty(layout.chevron) && ui::contains(layout.chevron, x, y)) {
    result.handled = true;
    const MenuId first = layout.overflow.front();
    if(ui.chrome.openMenu == first) closeMenu(ui);
    else {
      ui.chrome.openMenu = first;
      ui.chrome.menuHighlight = 0;
    }
    return result;
  }
  // The bar's own empty space. An open menu closes; otherwise this is the strip
  // a borderless window is dragged by, and the press belongs to the platform.
  if(ui.chrome.openMenu != MenuId::None) {
    closeMenu(ui);
    result.handled = true;
  }
  return result;
}

bool handleMenuBarMotion(TextRenderer& text, UiRuntime& ui, Rect rect, Rect bounds,
                         float x, float y) {
  if(ui.chrome.openMenu == MenuId::None) return false;

  // Sliding along the bar switches menus without a click. This is the whole of
  // what makes a menu bar feel like one rather than like seven buttons.
  const MenuBarLayout layout = layoutFor(text, ui, rect);
  for(const auto& item : layout.items) {
    if(!ui::contains(item.rect, x, y) || item.id == ui.chrome.openMenu) continue;
    ui.chrome.openMenu = item.id;
    ui.chrome.menuHighlight = 0;
    return true;
  }

  // Inside the popup the pointer owns the highlight, so a keyboard walk
  // followed by a mouse move does not leave two rows lit.
  const OpenMenu open = openMenuFor(text, ui, rect, bounds);
  if(open.items.empty()) return false;
  if(const auto index = ui::menuPopupItemAt(open.popup, open.items, x, y);
     index && *index != ui.chrome.menuHighlight) {
    ui.chrome.menuHighlight = *index;
    return true;
  }
  return false;
}

bool openMenuByKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift, bool alt) {
  if(ctrl) return false;
  // The first menu on the bar, which is the table's first and not the layout's:
  // a window too narrow to show File would otherwise put F10 on whatever
  // survived the overflow.
  if(key == SDLK_F10 && !alt && !shift) {
    const auto specs = ui::menuSpecs();
    if(specs.empty()) return false;
    if(ui.chrome.openMenu == specs.front().id) closeMenu(ui);
    else {
      ui.chrome.openMenu = specs.front().id;
      ui.chrome.menuHighlight = 0;
    }
    return true;
  }
  if(!alt || key < SDLK_A || key > SDLK_Z) return false;
  const MenuId id = ui::menuForMnemonic(static_cast<char>(key));
  if(id == MenuId::None) return false;
  // Alt+F on an open File menu shuts it, the way clicking File again does.
  if(ui.chrome.openMenu == id) closeMenu(ui);
  else {
    ui.chrome.openMenu = id;
    ui.chrome.menuHighlight = 0;
  }
  return true;
}

MenuBarKey handleMenuBarKey(TextRenderer& text, UiRuntime& ui, Rect rect, Rect bounds,
                            SDL_Keycode key) {
  MenuBarKey result;
  if(ui.chrome.openMenu == MenuId::None) return result;
  const OpenMenu open = openMenuFor(text, ui, rect, bounds);
  if(open.items.empty()) {
    closeMenu(ui);
    return result;
  }
  result.handled = true;

  const MenuBarLayout layout = layoutFor(text, ui, rect);
  const auto stepMenu = [&](int delta) {
    const auto specs = ui::menuSpecs();
    for(std::size_t i = 0; i < specs.size(); ++i) {
      if(specs[i].id != ui.chrome.openMenu) continue;
      const auto count = static_cast<int>(specs.size());
      const int next = (static_cast<int>(i) + delta + count) % count;
      ui.chrome.openMenu = specs[static_cast<std::size_t>(next)].id;
      ui.chrome.menuHighlight = 0;
      return;
    }
  };

  // Alt and a letter switches menus while one is open, the way sliding along
  // the bar with the button down does. Read from the modifier state because the
  // walk is given a keycode and not the mods that came with it.
  const SDL_Keymod mod = SDL_GetModState();
  if(openMenuByKey(ui, key, (mod & SDL_KMOD_CTRL) != 0, (mod & SDL_KMOD_SHIFT) != 0,
                   (mod & SDL_KMOD_ALT) != 0)) {
    return result;
  }

  switch(key) {
    case SDLK_ESCAPE:
      closeMenu(ui);
      return result;
    case SDLK_DOWN:
      ui.chrome.menuHighlight = stepHighlight(open.items, ui.chrome.menuHighlight, 1);
      return result;
    case SDLK_UP:
      ui.chrome.menuHighlight = stepHighlight(open.items, ui.chrome.menuHighlight, -1);
      return result;
    case SDLK_LEFT:
      stepMenu(-1);
      return result;
    case SDLK_RIGHT:
      stepMenu(1);
      return result;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
      if(ui.chrome.menuHighlight >= open.items.size()) {
        closeMenu(ui);
        return result;
      }
      const MenuItemSpec& spec = open.items[ui.chrome.menuHighlight];
      closeMenu(ui);
      if(!spec.separator && menuItemEnabled(ui, spec.action)) result.action = spec.action;
      return result;
    }
    default:
      break;
  }
  // Anything else is not the menu's: it closes and lets the key through, so
  // typing with a menu accidentally open does not silently go nowhere.
  (void)layout;
  closeMenu(ui);
  result.handled = false;
  return result;
}

bool menuBarHasControlAt(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  const MenuBarLayout layout = layoutFor(text, ui, rect);
  for(const auto& item : layout.items) {
    if(ui::contains(item.rect, x, y)) return true;
  }
  if(!ui::empty(layout.chevron) && ui::contains(layout.chevron, x, y)) return true;
  for(const Rect& button : layout.windowButtons) {
    if(ui::contains(ui::windowButtonHitRect(button), x, y)) return true;
  }
  return false;
}

}
