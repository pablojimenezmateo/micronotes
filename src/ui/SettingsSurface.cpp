#include "ui/SettingsSurface.h"

#include "core/util/Fuzzy.h"
#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

// A row matches when its label, its category or its description does.
//
// The description is included deliberately. Somebody who wants the text bigger
// types "bigger", or "size", or "font" -- and only one of those is in any label.
// The description is where the words people actually reach for live.
bool matches(std::string_view label, std::string_view category, std::string_view description,
             std::string_view query) {
  if(query.empty()) return true;
  return util::fuzzyScore(label, query).has_value() || util::fuzzyScore(category, query).has_value() ||
         util::fuzzyScore(description, query).has_value();
}

}

SettingsLayout settingsLayout(float windowWidth, float windowHeight, SettingsMode mode) {
  SettingsLayout layout;
  const float width = std::clamp(std::round(windowWidth * kSettingsWidthFraction),
                                 std::min(kSettingsMinWidth, std::max(0.0f, windowWidth - 32.0f)),
                                 kSettingsMaxWidth);
  const float height = std::max(std::min(kSettingsMinHeight, std::max(0.0f, windowHeight - 32.0f)),
                                std::round(windowHeight * kSettingsHeightFraction));
  layout.panel = {cardInsideWindow(std::round((windowWidth - width) / 2.0f), width, windowWidth),
                  cardInsideWindow(std::round((windowHeight - height) / 2.0f), height, windowHeight),
                  width, height};

  const Rect& panel = layout.panel;
  layout.header = {panel.x, panel.y, panel.w, kTitleBandHeight};
  layout.filter = {panel.x + kSpace3, layout.header.y + layout.header.h + kSpace2,
                   std::max(0.0f, panel.w - kSpace3 * 2.0f), kSettingsFilterHeight};
  layout.footer = {panel.x, panel.y + panel.h - kSettingsFooterHeight, panel.w, kSettingsFooterHeight};

  const float bodyTop = layout.filter.y + layout.filter.h + kSpace2;
  const float bodyHeight = std::max(0.0f, layout.footer.y - bodyTop);

  // About is one column: its rows are a label and a wrapped detail, and a rail
  // of categories over a list nobody groups would be a rail with one entry.
  if(mode == SettingsMode::About) {
    layout.rail = {};
    layout.sectionHeader = {};
    layout.values = {panel.x, bodyTop, panel.w, bodyHeight};
    return layout;
  }

  const float rail = std::min(kSettingsRailWidth, std::round(panel.w * 0.32f));
  layout.rail = {panel.x, bodyTop, rail, bodyHeight};
  const float valuesX = panel.x + rail + 1.0f;
  const float valuesW = std::max(0.0f, panel.x + panel.w - valuesX);
  layout.sectionHeader = {valuesX, bodyTop, valuesW, std::min(kSettingsSectionHeaderHeight, bodyHeight)};
  layout.values = {valuesX, bodyTop + layout.sectionHeader.h, valuesW,
                   std::max(0.0f, bodyHeight - layout.sectionHeader.h)};
  return layout;
}

std::vector<std::string> settingsCategories(const std::vector<SettingsRow>& rows) {
  std::vector<std::string> categories;
  for(const auto& row : rows) {
    if(std::find(categories.begin(), categories.end(), row.category) != categories.end()) continue;
    categories.push_back(row.category);
  }
  return categories;
}

std::vector<int> settingsRowsIn(const std::vector<SettingsRow>& rows, std::string_view category,
                                std::string_view query) {
  std::vector<int> found;
  for(int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto& row = rows[static_cast<std::size_t>(i)];
    if(row.category != category) continue;
    if(!matches(row.label, row.category, row.description, query)) continue;
    found.push_back(i);
  }
  return found;
}

bool settingsCategoryMatches(const std::vector<SettingsRow>& rows, std::string_view category,
                             std::string_view query) {
  // Not `!settingsRowsIn(...).empty()`, which is how it was written: the rail
  // asks this per category on every frame the card is up, and that spelling
  // builds a vector of the whole category to find out whether one row survived.
  return std::any_of(rows.begin(), rows.end(), [&](const SettingsRow& row) {
    return row.category == category && matches(row.label, row.category, row.description, query);
  });
}

SettingsSelection settingsSelection(const std::vector<SettingsRow>& rows, int category, int row,
                                    std::string_view query) {
  SettingsSelection selection;
  const auto categories = settingsCategories(rows);
  if(categories.empty()) return selection;
  selection.category = std::clamp(category, 0, static_cast<int>(categories.size()) - 1);
  selection.visible =
    settingsRowsIn(rows, categories[static_cast<std::size_t>(selection.category)], query);
  if(selection.visible.empty()) return selection;
  selection.row = std::clamp(row, 0, static_cast<int>(selection.visible.size()) - 1);
  return selection;
}

std::size_t settingsMatchCount(const std::vector<SettingsRow>& rows, std::string_view query) {
  std::size_t count = 0;
  for(const auto& row : rows) {
    if(matches(row.label, row.category, row.description, query)) ++count;
  }
  return count;
}

std::vector<int> aboutRowsMatching(const std::vector<AboutRow>& rows, std::string_view query) {
  std::vector<int> found;
  for(int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto& row = rows[static_cast<std::size_t>(i)];
    if(!matches(row.label, {}, row.detail, query)) continue;
    found.push_back(i);
  }
  return found;
}

}
