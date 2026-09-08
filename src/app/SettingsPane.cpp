#include "app/SettingsPane.h"

#include "app/Prompts.h"
#include "app/Shell.h"

#include "core/editor/SingleLineView.h"
#include "core/editor/SoftWrap.h"

#include "ui/Actions.h"
#include "app/RightPanel.h"
#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "core/platform/PathUtils.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace micronotes::app {
namespace {

using ui::SettingControl;
using ui::SettingsMode;
using ui::SettingsPaneFocus;

ui::TextStyle labelStyle() {
  return {ui::FontFamily::Sans, false, false, ui::type().ui};
}

ui::TextStyle helpStyle() {
  return {ui::FontFamily::Sans, false, false, ui::type().tiny};
}

ui::TextStyle titleStyle() {
  return {ui::FontFamily::Sans, true, false, ui::type().small};
}

// The controls a row reserves on its trailing edge, and the room the label and
// its help line are left with. One function because the click and the paint have
// to agree to the pixel about where a chip ends and the label may begin.
constexpr float kValueChipWidth = 108.0f;
constexpr float kStepperButtonWidth = 20.0f;
constexpr float kControlHeight = 22.0f;
constexpr float kResetSize = 18.0f;
constexpr float kRowPadX = 12.0f;
constexpr float kRowPadY = 7.0f;

ui::SettingsRowBoxes boxesFor(const ui::SettingsRow& row, Rect rect) {
  ui::SettingsRowBoxes boxes;
  boxes.row = rect;
  const float centreY = std::round(rect.y + kRowPadY - 1.0f);
  float right = rect.x + rect.w - kRowPadX;
  if(row.resettable) {
    boxes.reset = {right - kResetSize, centreY, kResetSize, kResetSize};
    right -= kResetSize + ui::kSpace2;
  }
  switch(row.control) {
    case SettingControl::Checkbox:
      boxes.checkbox = {right - 16.0f, centreY, 16.0f, 16.0f};
      break;
    case SettingControl::Segmented:
    case SettingControl::Action:
      boxes.value = {right - kValueChipWidth, centreY, kValueChipWidth, kControlHeight};
      break;
    case SettingControl::Stepper:
      boxes.next = {right - kStepperButtonWidth, centreY, kStepperButtonWidth, kControlHeight};
      boxes.value = {boxes.next.x - kValueChipWidth, centreY, kValueChipWidth, kControlHeight};
      boxes.previous = {boxes.value.x - kStepperButtonWidth, centreY, kStepperButtonWidth, kControlHeight};
      break;
    case SettingControl::None:
      break;
  }
  return boxes;
}

// Where a row's controls start, so the label and the help line know how much
// room they were left. Zero-width boxes -- the ones this row's control does not
// use -- are ignored rather than treated as sitting at x = 0.
float controlsLeft(const ui::SettingsRowBoxes& boxes, Rect rect) {
  float left = rect.x + rect.w - kRowPadX;
  for(const Rect* box : {&boxes.reset, &boxes.checkbox, &boxes.previous, &boxes.value}) {
    if(box->w > 0.0f) left = std::min(left, box->x);
  }
  return left;
}

std::vector<std::string> wrapHelp(ui::TextRenderer& text, std::string_view help, float width) {
  std::vector<std::string> lines;
  if(help.empty() || width < 40.0f) return lines;
  const auto style = helpStyle();
  for(const auto& row : editor::softWrap(help, static_cast<int>(width),
                                         [&](std::string_view value) { return text.width(value, style); })) {
    lines.push_back(row.text);
  }
  return lines;
}

float rowHeight(ui::TextRenderer& text, std::size_t helpLines) {
  return kRowPadY * 2.0f + static_cast<float>(text.lineHeight(labelStyle())) +
         static_cast<float>(helpLines) * static_cast<float>(text.lineHeight(helpStyle()));
}

// The next value in a cycle of three, forwards or back. Wrapping rather than
// clamping, because a stepper that stops at the end leaves you unable to tell a
// disabled arrow from a value that happens to be last.
int wrapStep(int value, int count, int direction) {
  if(count <= 0) return 0;
  return (value + direction + count) % count;
}

// One line about what each category is for, under its title. Not in the rows,
// because it is a property of the group rather than of any setting in it.
std::string_view categoryHelp(std::string_view category) {
  if(category == "Appearance") return "How the app looks and how large it reads.";
  if(category == "Workspace") return "Which panels are showing around the page.";
  if(category == "Library") return "Where the notes are, and what the keys do.";
  return {};
}

void clampSelection(UiRuntime& ui, const std::vector<ui::SettingsRow>& rows) {
  auto& surface = ui.settings;
  const auto selection =
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text());
  surface.category = selection.category;
  surface.row = selection.row;
}

}

void carryOutSettingsRequest(UiRuntime& ui, SettingsRequest request) {
  switch(request) {
    // A path, so it wants the text prompt that already exists for it rather
    // than a field wedged into a settings row.
    case SettingsRequest::LibraryFolder: openLibraryPrompt(ui); break;
    // The key reference is the other half of this same card.
    case SettingsRequest::Shortcuts: openAboutSurface(ui); break;
    case SettingsRequest::None: break;
  }
}

void openSettingsSurface(UiRuntime& ui) {
  ui.settings = {};
  ui.settings.visible = true;
  ui.settings.mode = SettingsMode::Settings;
  ui.settings.focus = SettingsPaneFocus::Values;
}

void openAboutSurface(UiRuntime& ui) {
  ui.settings = {};
  ui.settings.visible = true;
  ui.settings.mode = SettingsMode::About;
  // About is a list to read and to search, so the filter has the keyboard from
  // the start: it is the only thing on the card that takes typing.
  ui.settings.focus = SettingsPaneFocus::Filter;
}

void closeSettingsSurface(UiRuntime& ui) {
  ui.settings = {};
}

std::vector<ui::SettingsRow> settingsRows(const UiRuntime& ui) {
  std::vector<ui::SettingsRow> rows;
  const auto& workspace = ui.state.workspace();

  rows.push_back({"theme", "Appearance", "Theme",
                  "Light or dark. The palette is one hue with the lightness moved per role, so "
                  "both modes are the same design rather than two.",
                  SettingControl::Segmented, false,
                  ui::themeMode() == ui::ThemeMode::Dark ? "Dark" : "Light",
                  ui::themeMode() != ui::ThemeMode::Dark});
  rows.push_back({"text-size", "Appearance", "Text size",
                  "Scales every size in the type scale. The chrome stays put, so a larger size "
                  "buys bigger text rather than a bigger app.",
                  SettingControl::Stepper, false, std::string(ui::textSizeLabel(ui::textSize())),
                  ui::textSize() != ui::TextSize::Medium});
  rows.push_back({"page-width", "Appearance", "Page width",
                  "The widest the text column may grow. Extra room becomes margin rather than "
                  "more characters per line.",
                  SettingControl::Stepper, false, std::string(ui::pageWidthLabel(ui::pageWidth())),
                  ui::pageWidth() != ui::PageWidth::Medium});

  rows.push_back({"sidebar", "Workspace", "Sidebar",
                  "The notebooks, the search field and the tags, down the leading edge.",
                  SettingControl::Checkbox, workspace.sidebarVisible, {},
                  !workspace.sidebarVisible});
  rows.push_back({"right-panel", "Workspace", "Side panel",
                  "The outline, the links into this note, and its tags.",
                  SettingControl::Checkbox, workspace.rightPanelVisible, {},
                  workspace.rightPanelVisible});

  std::string library = ui.state.hasLibrary() ? platform::displayPath(ui.state.libraryRoot())
                                              : std::string("Not set");
  rows.push_back({"library", "Library", "Library folder",
                  "The one folder micronotes reads and writes. Plain Markdown files; nothing "
                  "leaves your disk.",
                  SettingControl::Action, false, std::move(library), false});
  rows.push_back({"shortcuts", "Library", "Keyboard shortcuts",
                  "Every command and the key it answers to, on the About page.",
                  SettingControl::Action, false, "Open", false});
  return rows;
}

std::vector<ui::AboutRow> aboutRows() {
  std::vector<ui::AboutRow> rows;
  rows.push_back({"micronotes", "A small Linux Markdown notes app. Plain .md files in one local "
                               "folder, and nothing leaves your disk."});
  rows.push_back({"Version", MICRONOTES_VERSION});

  // Then every binding the shell has, grouped the way the shortcut list grouped
  // them.
  //
  // This *is* the shortcut list. There used to be two of these -- an F1 overlay
  // that grouped every action by section, and an About page that listed the same
  // actions flat -- built from the same registry, in two shapes, in two files.
  // Neither was wrong and both had to be kept in step by hand, which is the
  // definition of the duplication worth removing. F1 opens this page.
  //
  // A row with no keys is a section heading: it carries a label and no detail,
  // which is how the two-column draw already renders a heading.
  for(int i = 0; i < static_cast<int>(ui::ActionSection::Count); ++i) {
    const auto section = static_cast<ui::ActionSection>(i);
    std::vector<ui::AboutRow> group;
    for(const auto& spec : ui::actionSpecs()) {
      if(spec.section != section) continue;
      const auto keys = ui::acceleratorText(spec);
      // An action with no key is in the palette and not in this list: a page of
      // "No key bound" tells you nothing about the keys.
      if(keys.empty()) continue;
      group.push_back({std::string(spec.label), keys});
    }
    // Keys that belong to a surface rather than to a command -- walking the
    // sidebar, continuing a list. The list would be lying by omission without
    // them, which is why `ui::helpRows` exists.
    for(const auto& row : ui::helpRows()) {
      if(row.section != section) continue;
      group.push_back({std::string(row.what), std::string(row.keys)});
    }
    if(group.empty()) continue;
    rows.push_back({std::string(ui::sectionLabel(section)), {}});
    for(auto& row : group) rows.push_back(std::move(row));
  }
  return rows;
}

void drawSettingsSurface(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                         int windowWidth, int windowHeight) {
  auto& surface = ui.settings;
  if(!surface.visible) return;

  const auto rows = settingsRows(ui);
  const auto about = aboutRows();
  clampSelection(ui, rows);
  const auto categories = ui::settingsCategories(rows);
  const std::string query = surface.query.text();

  ui::fill(renderer, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
           ui::theme().overlayBackdrop);
  const auto layout = ui::settingsLayout(static_cast<float>(windowWidth),
                                         static_cast<float>(windowHeight), surface.mode);
  surface.panel = layout.panel;
  surface.filter = layout.filter;
  surface.rail = layout.rail;
  surface.values = layout.values;
  surface.categoryRects.clear();
  surface.rowBoxes.clear();

  const Rect band = ui::drawTitledCard(renderer, layout.panel, layout.header.h);
  text.draw(surface.mode == SettingsMode::About ? "About micronotes" : "Settings",
            band.x + ui::kSpace3, ui::textTop(band, text, titleStyle()), ui::theme().accent,
            titleStyle());

  // --- the filter, in both modes ------------------------------------------
  {
    const bool focused = surface.focus == SettingsPaneFocus::Filter;
    ui::drawTextFieldFrame(renderer, layout.filter, focused);
    const float textY = ui::textTop(layout.filter, text, labelStyle());
    const float inner = layout.filter.w - ui::kSpace2 * 2.0f;
    if(surface.query.empty()) {
      text.draw(surface.mode == SettingsMode::About ? "Search commands and keys" : "Type to filter",
                layout.filter.x + ui::kSpace2, textY, ui::theme().textMuted, labelStyle());
    } else {
      const auto measure = [&](std::string_view value) { return text.width(value, labelStyle()); };
      const auto view = editor::layoutSingleLine(surface.query.editor, inner, surface.query.scrollX, measure);
      surface.query.scrollX = view.scrollX;
      const float left = layout.filter.x + ui::kSpace2 - view.scrollX;
      ui::ClipGuard clip(renderer, layout.filter);
      text.draw(query, left, textY, ui::theme().textPrimary, labelStyle());
      if(focused) {
        ui::fill(renderer, {left + view.caretX, layout.filter.y + 6.0f, 2.0f, layout.filter.h - 12.0f},
                 ui::theme().accent);
      }
    }
  }

  // --- the foot: what the filter left, and the way out --------------------
  {
    ui::fill(renderer, layout.footer, ui::theme().chromeBackground);
    ui::hLine(renderer, layout.footer.x, layout.footer.x + layout.footer.w, layout.footer.y,
              ui::theme().border);
    const auto style = helpStyle();
    const std::size_t shown = surface.mode == SettingsMode::About
                                ? ui::aboutRowsMatching(about, query).size()
                                : ui::settingsMatchCount(rows, query);
    const std::string count = std::to_string(shown) +
                              (surface.mode == SettingsMode::About
                                 ? (shown == 1 ? " entry" : " entries")
                                 : (shown == 1 ? " setting" : " settings"));
    text.draw(count, layout.footer.x + ui::kSpace3, ui::textTop(layout.footer, text, style),
              ui::theme().textMuted, style);
    const std::string hint = surface.mode == SettingsMode::About
                               ? "Esc close"
                               : "Tab pane   Up/Down move   Left/Right change   Esc close";
    const int width = text.width(hint, style);
    text.draw(hint, layout.footer.x + layout.footer.w - ui::kSpace3 - static_cast<float>(width),
              ui::textTop(layout.footer, text, style), ui::theme().textMuted, style);
  }

  // --- About: one column of label and wrapped detail ----------------------
  if(surface.mode == SettingsMode::About) {
    const auto visible = ui::aboutRowsMatching(about, query);
    const float labelColumn = std::round(layout.values.w * 0.42f);
    const float detailX = layout.values.x + ui::kSpace4 + labelColumn + ui::kSpace3;
    const float detailWidth = std::max(80.0f, layout.values.x + layout.values.w - ui::kSpace4 - detailX);
    const float step = static_cast<float>(text.lineHeight(helpStyle()));
    surface.aboutScroll = std::clamp(surface.aboutScroll, 0,
                                     std::max(0, static_cast<int>(visible.size()) - surface.aboutRowsShown));
    const float top = layout.values.y + ui::kSpace2;
    const float bottom = layout.values.y + layout.values.h;
    float y = top;
    int drawn = 0;
    {
      ui::ClipGuard clip(renderer, layout.values);
      for(std::size_t i = static_cast<std::size_t>(surface.aboutScroll); i < visible.size(); ++i) {
        const auto& row = about[static_cast<std::size_t>(visible[i])];
        const auto detail = wrapHelp(text, row.detail, detailWidth);
        const float height = std::max(static_cast<float>(text.lineHeight(labelStyle())),
                                      static_cast<float>(detail.size()) * step) + ui::kSpace1;
        // Stopped before it is drawn rather than after. Breaking on "the last row
        // started past the foot" leaves a row cut through the middle of its
        // glyphs, which reads as a rendering fault rather than as a list that
        // continues.
        if(drawn > 0 && y + height > bottom) break;
        // A row with no detail is a section heading -- there is no key to print
        // beside "Writing" -- so it is set in the accent and the strong face.
        // Drawn in the entry ink it read as one more command that happened to
        // have no shortcut, which is the opposite of what a heading is for.
        const bool heading = row.detail.empty();
        text.draw(ui::ellipsizeToWidth(text, row.label, static_cast<int>(labelColumn),
                                       heading ? titleStyle() : labelStyle()),
                  layout.values.x + ui::kSpace4, y,
                  heading ? ui::theme().accent : ui::theme().textPrimary,
                  heading ? titleStyle() : labelStyle());
        float detailY = y;
        for(const auto& line : detail) {
          text.draw(line, detailX, detailY, ui::theme().textSecondary, helpStyle());
          detailY += step;
        }
        y += height;
        ++drawn;
      }
    }
    surface.aboutRowsShown = std::max(1, drawn);
    if(visible.empty()) {
      text.draw("No matches", layout.values.x + ui::kSpace4, top, ui::theme().textMuted, labelStyle());
      return;
    }
    // The shell's scrollbar, in entries scaled to pixels: at `pitch` pixels an
    // entry the visible fraction and the thumb's travel come out the same as if
    // the list were measured, and there is no second scrollbar to keep in step.
    if(static_cast<int>(visible.size()) > drawn && drawn > 0) {
      const float pitch = (y - top) / static_cast<float>(drawn);
      const int hidden = static_cast<int>(visible.size()) - drawn;
      ui::drawVerticalScrollbar(renderer, layout.values,
                                static_cast<int>(std::lround(static_cast<float>(surface.aboutScroll) * pitch)),
                                static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
    }
    return;
  }

  // --- the rail of categories ---------------------------------------------
  ui::fill(renderer, {layout.values.x - 1.0f, layout.rail.y, 1.0f, layout.rail.h}, ui::theme().border);
  {
    const bool focused = surface.focus == SettingsPaneFocus::Categories;
    float y = layout.rail.y + ui::kSpace2;
    for(std::size_t i = 0; i < categories.size(); ++i) {
      const Rect rect {layout.rail.x + ui::kSpace1, y, layout.rail.w - ui::kSpace1 * 2.0f,
                       ui::kSettingsCategoryRowHeight};
      surface.categoryRects.push_back(rect);
      y += ui::kSettingsCategoryRowHeight;
      const bool selected = static_cast<int>(i) == surface.category;
      // A category the filter has emptied is listed and dimmed rather than
      // removed: a rail that reorders itself as you type is a rail you lose
      // your place in.
      const bool empty = !ui::settingsCategoryMatches(rows, categories[i], query);
      ui::drawRow(renderer, rect, selected, !selected && ui.pointer.over(rect));
      if(selected && focused) ui::drawFocusRing(renderer, rect, true);
      text.draw(ui::ellipsizeToWidth(text, categories[i], static_cast<int>(rect.w - ui::kSpace3 * 2.0f),
                                     labelStyle()),
                rect.x + ui::kSpace3, ui::textTop(rect, text, labelStyle()),
                empty ? ui::theme().textMuted : selected ? ui::theme().textPrimary : ui::theme().textSecondary, labelStyle());
    }
  }

  // --- the selected category's own heading --------------------------------
  const std::string category = categories.empty()
                                 ? std::string {}
                                 : categories[static_cast<std::size_t>(surface.category)];
  {
    text.draw(category, layout.sectionHeader.x + ui::kSpace4, layout.sectionHeader.y + ui::kSpace2,
              ui::theme().accent, titleStyle());
    text.draw(categoryHelp(category), layout.sectionHeader.x + ui::kSpace4,
              layout.sectionHeader.y + ui::kSpace2 + static_cast<float>(text.lineHeight(titleStyle())),
              ui::theme().textMuted, helpStyle());
    ui::hLine(renderer, layout.sectionHeader.x, layout.sectionHeader.x + layout.sectionHeader.w,
              layout.sectionHeader.y + layout.sectionHeader.h - 1.0f, ui::theme().border);
  }

  // --- the rows themselves ------------------------------------------------
  const auto visible = ui::settingsRowsIn(rows, category, query);
  surface.rowScroll = std::clamp(surface.rowScroll, 0,
                                 std::max(0, static_cast<int>(visible.size()) - surface.rowsShown));
  if(visible.empty()) {
    text.draw("Nothing here matches the filter", layout.values.x + ui::kSpace4,
              layout.values.y + ui::kSpace3, ui::theme().textMuted, labelStyle());
    return;
  }

  const bool focused = surface.focus == SettingsPaneFocus::Values;
  const float top = layout.values.y + ui::kSpace2;
  const float bottom = layout.values.y + layout.values.h;
  float y = top;
  int drawn = 0;
  ui::ClipGuard clip(renderer, layout.values);
  for(std::size_t i = static_cast<std::size_t>(surface.rowScroll); i < visible.size(); ++i) {
    const auto& row = rows[static_cast<std::size_t>(visible[i])];
    // Two passes over the help text: once to know how tall the row is, and once
    // to draw it. The row's height is its content's height, so a setting whose
    // explanation runs to two lines gets two lines rather than a clipped one.
    Rect rect {layout.values.x + ui::kSpace1, y, layout.values.w - ui::kSpace1 * 2.0f, 0.0f};
    auto boxes = boxesFor(row, {rect.x, rect.y, rect.w, rowHeight(text, 1)});
    const float labelX = rect.x + kRowPadX;
    const float helpWidth = std::max(60.0f, controlsLeft(boxes, rect) - ui::kSpace2 - labelX);
    const auto help = wrapHelp(text, row.description, helpWidth);
    rect.h = rowHeight(text, help.size());
    if(drawn > 0 && rect.y + rect.h > bottom) break;
    boxes = boxesFor(row, rect);
    surface.rowBoxes.push_back(boxes);
    y += rect.h;
    ++drawn;

    const bool selected = static_cast<int>(i) == surface.row;
    ui::drawRow(renderer, rect, selected, !selected && ui.pointer.over(rect));
    if(selected && focused) ui::drawFocusRing(renderer, rect, true);

    text.draw(ui::ellipsizeToWidth(text, row.label, static_cast<int>(helpWidth), labelStyle()), labelX,
              rect.y + kRowPadY, ui::theme().textPrimary, labelStyle());
    float helpY = rect.y + kRowPadY + static_cast<float>(text.lineHeight(labelStyle()));
    for(const auto& line : help) {
      text.draw(line, labelX, helpY, ui::theme().textMuted, helpStyle());
      helpY += static_cast<float>(text.lineHeight(helpStyle()));
    }

    if(row.resettable) {
      ui::drawSurface(renderer, boxes.reset, ui::theme().surfaceRaised, ui::theme().border);
      ui::drawResetGlyph(renderer, boxes.reset,
                         ui.pointer.over(boxes.reset) ? ui::theme().accent : ui::theme().textMuted);
      ui.pointer.offerTooltip(boxes.reset, "Back to the default");
    }

    switch(row.control) {
      case SettingControl::Checkbox:
        ui::drawSurface(renderer, boxes.checkbox, ui::theme().surfaceBackground,
                        row.checked ? ui::theme().accent : ui::theme().border);
        if(row.checked) ui::drawCheckGlyph(renderer, boxes.checkbox, ui::theme().accent);
        break;
      case SettingControl::Segmented:
      case SettingControl::Action:
        ui::drawButton(renderer, text, boxes.value,
                       ui::ellipsizeToWidth(text, row.value,
                                            static_cast<int>(boxes.value.w - ui::kSpace2 * 2.0f), labelStyle()),
                       true, ui.pointer.over(boxes.value), ui::ButtonTone::Neutral);
        break;
      case SettingControl::Stepper:
        ui::drawSurface(renderer, boxes.previous,
                        ui.pointer.over(boxes.previous) ? ui::theme().rowHighlight : ui::theme().surfaceRaised,
                        ui::theme().border);
        ui::drawArrowGlyph(renderer, boxes.previous, false, ui::theme().textSecondary);
        ui::drawButton(renderer, text, boxes.value, row.value, true,
                       ui.pointer.over(boxes.value), ui::ButtonTone::Neutral);
        ui::drawSurface(renderer, boxes.next,
                        ui.pointer.over(boxes.next) ? ui::theme().rowHighlight : ui::theme().surfaceRaised,
                        ui::theme().border);
        ui::drawArrowGlyph(renderer, boxes.next, true, ui::theme().textSecondary);
        break;
      case SettingControl::None:
        break;
    }
  }
  surface.rowsShown = std::max(1, drawn);
  if(static_cast<int>(visible.size()) > drawn && drawn > 0) {
    const float pitch = (y - top) / static_cast<float>(drawn);
    const int hidden = static_cast<int>(visible.size()) - drawn;
    ui::drawVerticalScrollbar(renderer, layout.values,
                              static_cast<int>(std::lround(static_cast<float>(surface.rowScroll) * pitch)),
                              static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
  }
}

namespace {

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
    surface.rowScroll = 0;
    return outcome;
  }

  const auto rows = settingsRows(ui);
  const auto selection =
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text());
  const auto& visible = selection.visible;
  for(std::size_t i = 0; i < surface.rowBoxes.size(); ++i) {
    const auto& boxes = surface.rowBoxes[i];
    if(!ui::contains(boxes.row, x, y)) continue;
    const std::size_t index = static_cast<std::size_t>(surface.rowScroll) + i;
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
    const int count = static_cast<int>(ui::aboutRowsMatching(about, surface.query.text()).size());
    const int last = std::max(0, count - surface.aboutRowsShown);
    if(key == SDLK_DOWN) surface.aboutScroll = std::min(surface.aboutScroll + 1, last);
    else if(key == SDLK_UP) surface.aboutScroll = std::max(0, surface.aboutScroll - 1);
    else {
      const auto handled = editor::applyKeyToField(surface.query, key, ctrl, shift);
      if(handled == editor::FieldKeyResult::Changed) surface.aboutScroll = 0;
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
      surface.rowScroll = 0;
    }
    return outcome;
  }

  if(surface.focus == SettingsPaneFocus::Categories) {
    if((key == SDLK_DOWN || key == SDLK_UP) && !categories.empty()) {
      const int count = static_cast<int>(categories.size());
      surface.category = (surface.category + (key == SDLK_DOWN ? 1 : -1) + count) % count;
      surface.row = 0;
      surface.rowScroll = 0;
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
    // Scroll to follow, one row at a time. The rows are not a fixed height, so
    // the draw is the only thing that knows how many fit -- and it clamps this
    // against what it actually placed.
    if(surface.row < surface.rowScroll) surface.rowScroll = surface.row;
    else if(surface.row >= surface.rowScroll + surface.rowsShown) {
      surface.rowScroll = surface.row - surface.rowsShown + 1;
    }
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
  surface.rowScroll = 0;
  surface.aboutScroll = 0;
  return true;
}

bool handleSettingsWheel(UiRuntime& ui, float dy) {
  auto& surface = ui.settings;
  if(!surface.visible) return false;
  const int notches = static_cast<int>(std::lround(dy * 2.0f));
  if(surface.mode == SettingsMode::About) {
    const auto about = aboutRows();
    const int count = static_cast<int>(ui::aboutRowsMatching(about, surface.query.text()).size());
    surface.aboutScroll = std::clamp(surface.aboutScroll - notches, 0,
                                     std::max(0, count - surface.aboutRowsShown));
    return true;
  }
  const auto rows = settingsRows(ui);
  const int count = static_cast<int>(
    ui::settingsSelection(rows, surface.category, surface.row, surface.query.text()).visible.size());
  surface.rowScroll = std::clamp(surface.rowScroll - notches, 0,
                                 std::max(0, count - surface.rowsShown));
  return true;
}

}
