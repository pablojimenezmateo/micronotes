#include "app/SettingsPane.h"

#include "app/Prompts.h"
#include "app/RightPanel.h"
#include "app/Shell.h"

#include "ui/Settings.h"
#include "ui/SettingsSurface.h"
#include "ui/Theme.h"

#include <algorithm>
#include <string>

// What a press or a keystroke on the Settings card means.
//
// The other half of `app/SettingsPane.h`, and split from it because the file
// was doing three jobs: building the rows, drawing them, and answering for
// them. The three do not share a helper -- the paint's are all about measuring
// text into a column, and these are all about stepping a value -- so the seam
// was already there, at 818 lines, waiting for somebody to cut along it.
//
// Every branch here is one call into the setting store or the workspace: the
// surface knows what the reader asked for, and the store knows what that
// means. Two rows are answers the card cannot give -- the library folder wants
// the text prompt that already exists, and the shortcut list is its own surface
// -- so those come back as a `SettingsRequest` for the shell to carry out
// rather than being reached into from here.
namespace micronotes::app {

using ui::SettingControl;
using ui::SettingsMode;
using ui::SettingsPaneFocus;

namespace {

// The next value in a cycle, forwards or back. Wrapping rather than clamping,
// because a stepper that stops at the end leaves you unable to tell a disabled
// arrow from a value that happens to be last.
int wrapStep(int value, int count, int direction) {
  if(count <= 0) return 0;
  return (value + direction + count) % count;
}

// Applies a change to the setting `id`, stepping by `direction` where the
// setting has an order to step along. Every branch is one call into the setting
// store or the workspace: the surface knows what the reader asked for, and the
// store knows what that means.
void applySetting(UiRuntime& ui, const std::string& id, int direction) {
  if(id == "theme") {
    ui::setThemeMode(ui::themeMode() == ui::ThemeMode::Dark ? ui::ThemeMode::Light : ui::ThemeMode::Dark);
    ui.status = ui::themeMode() == ui::ThemeMode::Dark ? "Dark theme" : "Light theme";
  } else if(id == "text-size") {
    const int next = wrapStep(static_cast<int>(ui::textSize()), 3, direction);
    ui::setTextSize(static_cast<ui::TextSize>(next));
    ui.status = "Text size " + std::string(ui::textSizeLabel(ui::textSize()));
  } else if(id == "page-width") {
    const int next = wrapStep(static_cast<int>(ui::pageWidth()), 3, direction);
    ui::setPageWidth(static_cast<ui::PageWidth>(next));
    ui.status = "Page width " + std::string(ui::pageWidthLabel(ui::pageWidth()));
  } else if(id == "sidebar") {
    togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar");
  } else if(id == "right-panel") {
    togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Outline panel");
  }
}

// Puts a setting back to what a fresh library starts with. Spelled out rather
// than derived from a default-constructed store: the two settings that live on
// the workspace have defaults that belong to the workspace, and reading them
// from a throwaway instance of it would tie this file to how that is built.
void resetSetting(UiRuntime& ui, const std::string& id) {
  if(id == "theme") ui::setThemeMode(ui::ThemeMode::Dark);
  else if(id == "text-size") ui::setTextSize(ui::TextSize::Medium);
  else if(id == "page-width") ui::setPageWidth(ui::PageWidth::Medium);
  else if(id == "sidebar") {
    if(!ui.state.workspace().sidebarVisible) togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar");
  } else if(id == "right-panel") {
    if(ui.state.workspace().rightPanelVisible) togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Outline panel");
  }
  ui.status = "Back to the default";
}

SettingsRequest requestFor(const std::string& id) {
  if(id == "library") return SettingsRequest::LibraryFolder;
  if(id == "shortcuts") return SettingsRequest::Shortcuts;
  return SettingsRequest::None;
}

}

SettingsOutcome handleSettingsClick(UiRuntime& ui, float x, float y) {
  SettingsOutcome outcome;
  auto& surface = ui.settings;
  if(!surface.visible) return outcome;
  outcome.handled = true;

  if(!ui::contains(surface.panel, x, y)) {
    closeSettingsSurface(ui);
    return outcome;
  }
  if(ui::contains(surface.filter, x, y)) {
    surface.focus = SettingsPaneFocus::Filter;
    return outcome;
  }
  if(surface.mode == SettingsMode::About) return outcome;

  for(std::size_t i = 0; i < surface.categoryRects.size(); ++i) {
    if(!ui::contains(surface.categoryRects[i], x, y)) continue;
    surface.focus = SettingsPaneFocus::Categories;
    surface.category = static_cast<int>(i);
    surface.row = 0;
    surface.rows.rebase();
    return outcome;
  }

  const auto rows = settingsRows(ui);
  const auto selection =
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text());
  const auto& visible = selection.visible;
  for(std::size_t i = 0; i < surface.rowBoxes.size(); ++i) {
    const auto& boxes = surface.rowBoxes[i];
    if(!ui::contains(boxes.row, x, y)) continue;
    const std::size_t index = static_cast<std::size_t>(surface.rows.scroll) + i;
    if(index >= visible.size()) break;
    const auto& row = rows[static_cast<std::size_t>(visible[index])];
    surface.focus = SettingsPaneFocus::Values;
    surface.row = static_cast<int>(index);
    // The reset first: it sits inside the row, and a row-wide click that also
    // counted as a reset would make the button impossible to aim at.
    if(row.resettable && ui::contains(boxes.reset, x, y)) {
      resetSetting(ui, row.id);
      return outcome;
    }
    if(boxes.previous.w > 0.0f && ui::contains(boxes.previous, x, y)) {
      applySetting(ui, row.id, -1);
      return outcome;
    }
    if(boxes.next.w > 0.0f && ui::contains(boxes.next, x, y)) {
      applySetting(ui, row.id, 1);
      return outcome;
    }
    if(row.control == SettingControl::Action &&
       (ui::contains(boxes.value, x, y) || ui::contains(boxes.row, x, y))) {
      outcome.request = requestFor(row.id);
      if(outcome.request != SettingsRequest::None) closeSettingsSurface(ui);
      return outcome;
    }
    // A checkbox or a chip answers to a click anywhere on its row. The control
    // is 16 pixels of a 40-pixel row, and a preference you have to hit exactly
    // is a preference people give up on.
    if(row.control == SettingControl::Checkbox || row.control == SettingControl::Segmented ||
       row.control == SettingControl::Stepper) {
      applySetting(ui, row.id, 1);
    }
    return outcome;
  }
  return outcome;
}

SettingsOutcome handleSettingsKey(UiRuntime& ui, SDL_Keycode key, bool ctrl, bool shift) {
  SettingsOutcome outcome;
  auto& surface = ui.settings;
  if(!surface.visible) return outcome;
  outcome.handled = true;

  if(key == SDLK_ESCAPE) {
    closeSettingsSurface(ui);
    return outcome;
  }
  if(key == SDLK_TAB) {
    // Three panes in a ring. About has one of them, so Tab there does nothing
    // rather than cycling through two panes it does not draw.
    if(surface.mode == SettingsMode::About) return outcome;
    const int panes = 3;
    const int at = static_cast<int>(surface.focus);
    surface.focus = static_cast<SettingsPaneFocus>((at + (shift ? -1 : 1) + panes) % panes);
    return outcome;
  }

  const auto rows = settingsRows(ui);
  const auto categories = ui::settingsCategories(rows);

  if(surface.mode == SettingsMode::About) {
    const auto about = aboutRows();
    const std::size_t count = ui::aboutRowsMatching(about, surface.query.text()).size();
    if(key == SDLK_DOWN) surface.about.scrollBy(1, count);
    else if(key == SDLK_UP) surface.about.scrollBy(-1, count);
    else {
      const auto handled = editor::applyKeyToField(surface.query, key, ctrl, shift);
      if(handled == editor::FieldKeyResult::Changed) surface.about.rebase();
    }
    return outcome;
  }

  if(surface.focus == SettingsPaneFocus::Filter) {
    if(key == SDLK_DOWN) {
      surface.focus = SettingsPaneFocus::Values;
      return outcome;
    }
    const auto handled = editor::applyKeyToField(surface.query, key, ctrl, shift);
    if(handled == editor::FieldKeyResult::Changed) {
      surface.row = 0;
      surface.rows.rebase();
    }
    return outcome;
  }

  if(surface.focus == SettingsPaneFocus::Categories) {
    if((key == SDLK_DOWN || key == SDLK_UP) && !categories.empty()) {
      const int count = static_cast<int>(categories.size());
      surface.category = (surface.category + (key == SDLK_DOWN ? 1 : -1) + count) % count;
      surface.row = 0;
      surface.rows.rebase();
    } else if(key == SDLK_RIGHT || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      surface.focus = SettingsPaneFocus::Values;
    }
    return outcome;
  }

  // The values pane.
  const auto selection =
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text());
  if(key == SDLK_LEFT && selection.empty()) {
    surface.focus = SettingsPaneFocus::Categories;
    return outcome;
  }
  if(selection.empty()) return outcome;
  const int count = static_cast<int>(selection.visible.size());
  surface.row = selection.row;
  const auto& row = rows[static_cast<std::size_t>(selection.selected())];

  if(key == SDLK_DOWN || key == SDLK_UP) {
    surface.row = (surface.row + (key == SDLK_DOWN ? 1 : -1) + count) % count;
    // Scroll to follow. The rows are not a fixed height, so the draw is the
    // only thing that knows how many fit, and `RowStrip::shown` is what it
    // recorded.
    surface.rows.reveal(surface.row);
    return outcome;
  }
  if(key == SDLK_LEFT || key == SDLK_RIGHT) {
    // Left off a stepper's first value steps it backwards; on a row with no
    // control at all it goes back to the rail, which is the only way Left could
    // usefully mean anything there.
    if(row.control == SettingControl::None || row.control == SettingControl::Action) {
      if(key == SDLK_LEFT) surface.focus = SettingsPaneFocus::Categories;
      return outcome;
    }
    applySetting(ui, row.id, key == SDLK_RIGHT ? 1 : -1);
    return outcome;
  }
  if(key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
    if(row.control == SettingControl::Action) {
      outcome.request = requestFor(row.id);
      if(outcome.request != SettingsRequest::None) closeSettingsSurface(ui);
      return outcome;
    }
    applySetting(ui, row.id, 1);
    return outcome;
  }
  if(key == SDLK_BACKSPACE || key == SDLK_DELETE) {
    if(row.resettable) resetSetting(ui, row.id);
    return outcome;
  }
  return outcome;
}

bool handleSettingsText(UiRuntime& ui, const char* input) {
  auto& surface = ui.settings;
  if(!surface.visible || !input) return surface.visible;
  // Typing goes to the filter wherever the keyboard nominally is, and moves it
  // there. A modal with a search field that only accepts text once you have
  // clicked into it is a modal that appears not to work.
  surface.focus = SettingsPaneFocus::Filter;
  surface.query.editor.insert(input);
  surface.row = 0;
  surface.rows.rebase();
  surface.about.rebase();
  return true;
}

bool handleSettingsWheel(UiRuntime& ui, float dy) {
  auto& surface = ui.settings;
  if(!surface.visible) return false;
  const int notches = static_cast<int>(std::lround(dy * 2.0f));
  if(surface.mode == SettingsMode::About) {
    const auto about = aboutRows();
    surface.about.scrollBy(-notches, ui::aboutRowsMatching(about, surface.query.text()).size());
    return true;
  }
  const auto rows = settingsRows(ui);
  surface.rows.scrollBy(
    -notches,
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text()).visible.size());
  return true;
}

}
