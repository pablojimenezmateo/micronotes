#include "app/SettingsPane.h"

#include "app/EditCommands.h"

#include "app/Prompts.h"
#include "app/Shell.h"

#include "core/editor/SingleLineView.h"
#include "core/editor/SoftWrap.h"

#include "ui/Actions.h"
#include "app/RightPanel.h"
#include "ui/Metrics.h"
#include "ui/Settings.h"
#include "core/platform/PathUtils.h"
#include "ui/RowCursor.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/Widgets.h"
#include "ui/ClipGuard.h"

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

// One line about what each category is for, under its title. Not in the rows,
// because it is a property of the group rather than of any setting in it.
std::string_view categoryHelp(std::string_view category) {
  if(category == "Appearance") return "How the app looks and how large it reads.";
  if(category == "Workspace") return "Which panels are showing around the page.";
  if(category == "Library") return "Where the notes are, and what the keys do.";
  return {};
}

// What measuring a settings row produced, handed to the paint so the help text
// is wrapped once rather than once per pass.
struct MeasuredSettingRow {
  // The room the controls left the label and the help, which both the wrap and
  // the label's ellipsis are cut to.
  float helpWidth = 0.0f;
  std::vector<std::string> help;
};

// Painting one of the card's two scrolling lists.
//
// The About entries and the settings rows are the same eleven statements --
// clamp the offset, walk from it, stop before the foot cuts a row, record how
// many fitted, scale the offset to pixels for the scrollbar -- around a
// different row body, and they were written out twice, over two sets of fields
// for the same two concepts.
//
// The count is load-bearing rather than cosmetic. A row is as tall as its own
// content, so only the paint can know how many the pane held, and the wheel and
// the arrow keys clamp against what it recorded: two copies that disagree are a
// list you cannot scroll to the end of, not a wrong pixel.
//
// Measured and painted in two steps because a row's height *is* its wrapped
// text, and wrapping it twice would measure the whole list twice: `measure`
// returns the height and whatever it had to build to know it, and `paint` is
// handed both back with the box they landed in.
template <typename Measure, typename Paint>
void drawScrollingRows(SDL_Renderer* renderer, ui::RowStrip& strip, Rect values, float inset,
                       std::size_t count, const Measure& measure, const Paint& paint) {
  strip.clamp(count);
  ui::RowCursor cursor(values.x, values.w, values.y + ui::kSpace2);
  cursor.stopAt(values.y + values.h);
  {
    ui::ClipGuard clip(renderer, values);
    for(std::size_t i = static_cast<std::size_t>(strip.scroll); i < count; ++i) {
      auto [height, row] = measure(i);
      if(!cursor.fits(height)) break;
      paint(i, row, cursor.place(height, inset));
    }
  }
  strip.fitted(cursor.placed());
  // The shell's scrollbar, in rows scaled to pixels: at `cursor.pitch()` pixels
  // a row the visible fraction and the thumb's travel come out the same as if
  // the list had been measured, and there is no second scrollbar to keep in
  // step with this one.
  const int hidden = strip.last(count);
  if(hidden <= 0) return;
  const float pitch = cursor.pitch();
  ui::drawVerticalScrollbar(renderer, values,
                            static_cast<int>(std::lround(static_cast<float>(strip.scroll) * pitch)),
                            static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
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

  std::string library = ui.state.catalog().isOpen() ? platform::displayPath(ui.state.catalog().root())
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
    // The block shapes a digit produces, one row each, read from the table that
    // actually binds them.
    //
    // These were three rows in two hand-written tables and they disagreed:
    // `turn-into`'s hint said "Ctrl+Shift+1-9" while 4, 5 and 6 were bound to
    // nothing, and two `helpRows` entries described the same digits again in
    // two more spellings -- so the shortcut list showed this one family three
    // times and no two of them agreed. Derived, it says which digit makes which
    // shape, which is what a reader wanted from it, and it cannot drift from
    // the keyboard because it is the same table the keyboard reads.
    if(section == ui::ActionSection::Blocks) {
      for(const auto& entry : blockKinds()) {
        if(entry.chordDigit == 0) continue;
        ui::KeyChord chord;
        chord.ctrl = true;
        chord.shift = true;
        chord.key = static_cast<SDL_Keycode>(entry.chordDigit);
        group.push_back({std::string("Turn into ") + entry.label, ui::formatKeyChord(chord)});
      }
    }
    if(group.empty()) continue;
    rows.push_back({std::string(ui::sectionLabel(section)), {}});
    for(auto& row : group) rows.push_back(std::move(row));
  }
  return rows;
}

namespace {

// The filter, in both modes. The one thing on the card that takes typing, so it
// is also the one part that lays a caret out.
void drawFilterField(SDL_Renderer* renderer, ui::TextRenderer& text,
                     ui::SettingsSurfaceState& surface, Rect field) {
  const bool focused = surface.focus == SettingsPaneFocus::Filter;
  ui::drawTextFieldFrame(renderer, field, focused);
  ui::TextFieldPaint paint;
  paint.box = field;
  paint.textY = ui::textTop(field, text, labelStyle());
  paint.padX = ui::kSpace2;
  paint.insetY = 6.0f;
  paint.placeholder = surface.mode == SettingsMode::About ? "Search commands and keys"
                                                          : "Type to filter";
  paint.focused = focused;
  ui::drawTextFieldText(renderer, text, labelStyle(), surface.query, paint);
}

// The foot: what the filter left, and the way out.
void drawFoot(SDL_Renderer* renderer, ui::TextRenderer& text, SettingsMode mode, Rect footer,
              std::size_t shown) {
  ui::fill(renderer, footer, ui::theme().chromeBackground);
  ui::hLine(renderer, footer.x, footer.x + footer.w, footer.y, ui::theme().border);
  const auto style = helpStyle();
  const std::string count = std::to_string(shown) +
                            (mode == SettingsMode::About ? (shown == 1 ? " entry" : " entries")
                                                         : (shown == 1 ? " setting" : " settings"));
  text.draw(count, footer.x + ui::kSpace3, ui::textTop(footer, text, style), ui::theme().textMuted,
            style);
  const std::string hint = mode == SettingsMode::About
                             ? "Esc close"
                             : "Tab pane   Up/Down move   Left/Right change   Esc close";
  const int width = text.width(hint, style);
  text.draw(hint, footer.x + footer.w - ui::kSpace3 - static_cast<float>(width),
            ui::textTop(footer, text, style), ui::theme().textMuted, style);
}

// About: one column of label and wrapped detail.
void drawAboutList(SDL_Renderer* renderer, ui::TextRenderer& text, ui::RowStrip& strip, Rect values,
                   const std::vector<ui::AboutRow>& about, const std::vector<int>& visible) {
  if(visible.empty()) {
    text.draw("No matches", values.x + ui::kSpace4, values.y + ui::kSpace2, ui::theme().textMuted,
              labelStyle());
    return;
  }
  const float labelColumn = std::round(values.w * 0.42f);
  const float detailX = values.x + ui::kSpace4 + labelColumn + ui::kSpace3;
  const float detailWidth = std::max(80.0f, values.x + values.w - ui::kSpace4 - detailX);
  const float step = static_cast<float>(text.lineHeight(helpStyle()));
  drawScrollingRows(
    renderer, strip, values, 0.0f, visible.size(),
    [&](std::size_t i) {
      auto detail = wrapHelp(text, about[static_cast<std::size_t>(visible[i])].detail, detailWidth);
      const float height = std::max(static_cast<float>(text.lineHeight(labelStyle())),
                                    static_cast<float>(detail.size()) * step) + ui::kSpace1;
      return std::pair {height, std::move(detail)};
    },
    [&](std::size_t i, const std::vector<std::string>& detail, Rect rect) {
      const auto& row = about[static_cast<std::size_t>(visible[i])];
      // A row with no detail is a section heading -- there is no key to print
      // beside "Writing" -- so it is set in the accent and the strong face.
      // Drawn in the entry ink it read as one more command that happened to
      // have no shortcut, which is the opposite of what a heading is for.
      const bool heading = row.detail.empty();
      text.draw(ui::ellipsizeToWidth(text, row.label, static_cast<int>(labelColumn),
                                     heading ? titleStyle() : labelStyle()),
                values.x + ui::kSpace4, rect.y,
                heading ? ui::theme().accent : ui::theme().textPrimary,
                heading ? titleStyle() : labelStyle());
      float detailY = rect.y;
      for(const auto& line : detail) {
        text.draw(line, detailX, detailY, ui::theme().textSecondary, helpStyle());
        detailY += step;
      }
    });
}

// The rail of categories, and the rects a click on one lands in.
void drawCategoryRail(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rail,
                      const std::vector<ui::SettingsRow>& rows,
                      const std::vector<std::string>& categories, std::string_view query) {
  auto& surface = ui.settings;
  const bool focused = surface.focus == SettingsPaneFocus::Categories;
  ui::RowCursor cursor(rail.x, rail.w, rail.y + ui::kSpace2);
  for(std::size_t i = 0; i < categories.size(); ++i) {
    const Rect rect = cursor.place(ui::kSettingsCategoryRowHeight, ui::kSpace1);
    surface.categoryRects.push_back(rect);
    const bool selected = static_cast<int>(i) == surface.category;
    // A category the filter has emptied is listed and dimmed rather than
    // removed: a rail that reorders itself as you type is a rail you lose your
    // place in.
    const bool empty = !ui::settingsCategoryMatches(rows, categories[i], query);
    ui::drawRow(renderer, rect, selected, !selected && ui.pointer.over(rect));
    if(selected && focused) ui::drawFocusRing(renderer, rect, true);
    text.draw(ui::ellipsizeToWidth(text, categories[i],
                                   static_cast<int>(rect.w - ui::kSpace3 * 2.0f), labelStyle()),
              rect.x + ui::kSpace3, ui::textTop(rect, text, labelStyle()),
              empty ? ui::theme().textMuted
                    : selected ? ui::theme().textPrimary : ui::theme().textSecondary,
              labelStyle());
  }
}

// The selected category's own heading, and the line under it.
void drawCategoryHeading(SDL_Renderer* renderer, ui::TextRenderer& text, Rect header,
                         std::string_view category) {
  text.draw(category, header.x + ui::kSpace4, header.y + ui::kSpace2, ui::theme().accent,
            titleStyle());
  text.draw(categoryHelp(category), header.x + ui::kSpace4,
            header.y + ui::kSpace2 + static_cast<float>(text.lineHeight(titleStyle())),
            ui::theme().textMuted, helpStyle());
  ui::hLine(renderer, header.x, header.x + header.w, header.y + header.h - 1.0f,
            ui::theme().border);
}

// The settings themselves: a label, its help wrapped to what the controls left,
// and the controls.
void drawValueRows(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect values,
                   const std::vector<ui::SettingsRow>& rows, const std::vector<int>& visible) {
  auto& surface = ui.settings;
  if(visible.empty()) {
    surface.rows.rebase();
    text.draw("Nothing here matches the filter", values.x + ui::kSpace4, values.y + ui::kSpace3,
              ui::theme().textMuted, labelStyle());
    return;
  }
  const bool focused = surface.focus == SettingsPaneFocus::Values;
  drawScrollingRows(
    renderer, surface.rows, values, ui::kSpace1, visible.size(),
    [&](std::size_t i) {
      const auto& row = rows[static_cast<std::size_t>(visible[i])];
      // The row's height is its content's height, so a setting whose
      // explanation runs to two lines gets two lines rather than a clipped one.
      // The controls are placed against a one-line probe first, because how
      // much room the help has to wrap into is what the controls left it.
      const Rect column {values.x + ui::kSpace1, values.y, values.w - ui::kSpace1 * 2.0f,
                         rowHeight(text, 1)};
      MeasuredSettingRow measured;
      measured.helpWidth = std::max(60.0f, controlsLeft(boxesFor(row, column), column) -
                                             ui::kSpace2 - (column.x + kRowPadX));
      measured.help = wrapHelp(text, row.description, measured.helpWidth);
      return std::pair {rowHeight(text, measured.help.size()), std::move(measured)};
    },
    [&](std::size_t i, const MeasuredSettingRow& measured, Rect rect) {
      const auto& row = rows[static_cast<std::size_t>(visible[i])];
      const auto boxes = boxesFor(row, rect);
      surface.rowBoxes.push_back(boxes);

      const bool selected = static_cast<int>(i) == surface.row;
      ui::drawRow(renderer, rect, selected, !selected && ui.pointer.over(rect));
      if(selected && focused) ui::drawFocusRing(renderer, rect, true);

      const float labelX = rect.x + kRowPadX;
      text.draw(ui::ellipsizeToWidth(text, row.label, static_cast<int>(measured.helpWidth), labelStyle()),
                labelX, rect.y + kRowPadY, ui::theme().textPrimary, labelStyle());
      float helpY = rect.y + kRowPadY + static_cast<float>(text.lineHeight(labelStyle()));
      for(const auto& line : measured.help) {
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
          ui::drawArrowGlyph(renderer, boxes.previous, ui::ArrowDirection::Left, ui::theme().textSecondary);
          ui::drawButton(renderer, text, boxes.value, row.value, true,
                         ui.pointer.over(boxes.value), ui::ButtonTone::Neutral);
          ui::drawSurface(renderer, boxes.next,
                          ui.pointer.over(boxes.next) ? ui::theme().rowHighlight : ui::theme().surfaceRaised,
                          ui::theme().border);
          ui::drawArrowGlyph(renderer, boxes.next, ui::ArrowDirection::Right, ui::theme().textSecondary);
          break;
        case SettingControl::None:
          break;
      }
    });
}

}

// The card, band by band. Each band is a function above rather than a paragraph
// here: the surface has six of them, two of which differ between the two modes,
// and written inline that came to one 262-line function with the mode branch
// buried two thirds of the way down it.
void drawSettingsSurface(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                         int windowWidth, int windowHeight) {
  auto& surface = ui.settings;
  if(!surface.visible) return;

  const auto rows = settingsRows(ui);
  clampSelection(ui, rows);
  const std::string query = surface.query.text();
  const bool aboutMode = surface.mode == SettingsMode::About;

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
  text.draw(aboutMode ? "About micronotes" : "Settings", band.x + ui::kSpace3,
            ui::textTop(band, text, titleStyle()), ui::theme().accent, titleStyle());
  drawFilterField(renderer, text, surface, layout.filter);

  if(aboutMode) {
    const auto about = aboutRows();
    const auto visible = ui::aboutRowsMatching(about, query);
    drawFoot(renderer, text, surface.mode, layout.footer, visible.size());
    drawAboutList(renderer, text, surface.about, layout.values, about, visible);
    return;
  }

  drawFoot(renderer, text, surface.mode, layout.footer, ui::settingsMatchCount(rows, query));

  const auto categories = ui::settingsCategories(rows);
  ui::fill(renderer, {layout.values.x - 1.0f, layout.rail.y, 1.0f, layout.rail.h},
           ui::theme().border);
  drawCategoryRail(renderer, text, ui, layout.rail, rows, categories, query);

  const std::string category = categories.empty()
                                 ? std::string {}
                                 : categories[static_cast<std::size_t>(surface.category)];
  drawCategoryHeading(renderer, text, layout.sectionHeader, category);
  drawValueRows(renderer, text, ui, layout.values, rows,
                ui::settingsRowsIn(rows, category, query));
}


}
