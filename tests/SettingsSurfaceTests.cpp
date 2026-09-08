#include "TestSupport.h"

#include "ui/SettingsSurface.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using micronotes::ui::AboutRow;
using micronotes::ui::SettingControl;
using micronotes::ui::SettingsMode;
using micronotes::ui::SettingsRow;

// A stand-in for what `app::settingsRows` builds, so the model can be tested
// without a window, a theme or a library on disk.
std::vector<SettingsRow> sampleRows() {
  return {
    {"theme", "Appearance", "Theme", "Light or dark.", SettingControl::Segmented, false, "Dark", false},
    {"text-size", "Appearance", "Text size", "How large the body text reads.",
     SettingControl::Stepper, false, "Medium", true},
    {"sidebar", "Workspace", "Sidebar", "The notebooks down the leading edge.",
     SettingControl::Checkbox, true, {}, false},
    {"library", "Library", "Library folder", "The one folder micronotes reads and writes.",
     SettingControl::Action, false, "~/Notes", false},
  };
}

bool contains(const std::vector<std::string>& values, std::string_view wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

}

MICRONOTES_TEST(settings_categories_come_out_in_the_order_the_rows_are_in) {
  const auto categories = micronotes::ui::settingsCategories(sampleRows());
  MICRONOTES_REQUIRE(categories.size() == 3);
  MICRONOTES_REQUIRE(categories[0] == "Appearance");
  MICRONOTES_REQUIRE(categories[1] == "Workspace");
  MICRONOTES_REQUIRE(categories[2] == "Library");
}

MICRONOTES_TEST(settings_categories_are_listed_once_each) {
  auto rows = sampleRows();
  rows.push_back({"another", "Appearance", "Another", "", SettingControl::None, false, {}, false});
  const auto categories = micronotes::ui::settingsCategories(rows);
  MICRONOTES_REQUIRE(std::count(categories.begin(), categories.end(), "Appearance") == 1);
}

MICRONOTES_TEST(an_empty_filter_matches_every_row_of_a_category) {
  const auto rows = sampleRows();
  MICRONOTES_REQUIRE(micronotes::ui::settingsRowsIn(rows, "Appearance", "").size() == 2);
  MICRONOTES_REQUIRE(micronotes::ui::settingsRowsIn(rows, "Workspace", "").size() == 1);
  MICRONOTES_REQUIRE(micronotes::ui::settingsMatchCount(rows, "") == rows.size());
}

MICRONOTES_TEST(the_filter_reaches_a_row_description_and_not_only_its_label) {
  // Somebody who wants the text bigger types "large", or "reads", and neither is
  // in any label. The description is where the words people reach for live, and
  // a filter that could not see it made the card searchable in name only.
  const auto rows = sampleRows();
  const auto found = micronotes::ui::settingsRowsIn(rows, "Appearance", "reads");
  MICRONOTES_REQUIRE(found.size() == 1);
  MICRONOTES_REQUIRE(rows[static_cast<std::size_t>(found.front())].id == "text-size");
}

MICRONOTES_TEST(the_filter_reaches_a_row_category) {
  const auto rows = sampleRows();
  MICRONOTES_REQUIRE(micronotes::ui::settingsRowsIn(rows, "Workspace", "workspace").size() == 1);
}

MICRONOTES_TEST(a_filter_that_matches_nothing_empties_every_category) {
  const auto rows = sampleRows();
  for(const auto& category : micronotes::ui::settingsCategories(rows)) {
    micronotes::tests::require(
      !micronotes::ui::settingsCategoryMatches(rows, category, "zzzqqqxxx"),
      category + " still matches a query nothing should match");
  }
  MICRONOTES_REQUIRE(micronotes::ui::settingsMatchCount(rows, "zzzqqqxxx") == 0);
}

MICRONOTES_TEST(a_category_reports_whether_the_filter_left_it_anything) {
  const auto rows = sampleRows();
  MICRONOTES_REQUIRE(micronotes::ui::settingsCategoryMatches(rows, "Appearance", "theme"));
  // Listed but empty rather than removed: a rail that reorders itself as you
  // type is a rail you lose your place in.
  MICRONOTES_REQUIRE(!micronotes::ui::settingsCategoryMatches(rows, "Workspace", "theme"));
  MICRONOTES_REQUIRE(contains(micronotes::ui::settingsCategories(rows), "Workspace"));
}

MICRONOTES_TEST(about_rows_are_filtered_on_their_detail_as_well_as_their_label) {
  const std::vector<AboutRow> rows {
    {"micronotes", "A small Linux Markdown notes app."},
    {"Version", "0.1.3"},
    {"Go to note...", "Ctrl+P"},
  };
  MICRONOTES_REQUIRE(micronotes::ui::aboutRowsMatching(rows, "").size() == 3);
  // By label...
  MICRONOTES_REQUIRE(micronotes::ui::aboutRowsMatching(rows, "version").size() == 1);
  // ...and by the keys in the detail column, which is how somebody looks up
  // what a chord they half-remember actually does.
  const auto byKey = micronotes::ui::aboutRowsMatching(rows, "Markdown");
  MICRONOTES_REQUIRE(byKey.size() == 1);
  MICRONOTES_REQUIRE(rows[static_cast<std::size_t>(byKey.front())].label == "micronotes");
}

MICRONOTES_TEST(the_settings_card_is_centred_and_bounded) {
  const auto layout = micronotes::ui::settingsLayout(1600.0f, 1000.0f, SettingsMode::Settings);
  MICRONOTES_REQUIRE(layout.panel.w <= micronotes::ui::kSettingsMaxWidth);
  // Centred to the pixel: the two margins agree.
  const float left = layout.panel.x;
  const float right = 1600.0f - (layout.panel.x + layout.panel.w);
  micronotes::tests::require(std::abs(left - right) <= 1.0f, "the card is not centred");
}

MICRONOTES_TEST(the_settings_card_stays_on_a_small_window) {
  const auto layout = micronotes::ui::settingsLayout(640.0f, 400.0f, SettingsMode::Settings);
  micronotes::tests::require(layout.panel.x >= 0.0f && layout.panel.y >= 0.0f,
                             "the card was placed off the top or the left of the window");
  micronotes::tests::require(layout.panel.x + layout.panel.w <= 640.0f + 1.0f,
                             "the card runs off the right of the window");
  micronotes::tests::require(layout.panel.y + layout.panel.h <= 400.0f + 1.0f,
                             "the card runs off the bottom of the window");
}

MICRONOTES_TEST(the_settings_card_bands_stack_without_overlapping) {
  const auto layout = micronotes::ui::settingsLayout(1400.0f, 900.0f, SettingsMode::Settings);
  micronotes::tests::require(layout.filter.y >= layout.header.y + layout.header.h,
                             "the filter is drawn over the header band");
  micronotes::tests::require(layout.rail.y >= layout.filter.y + layout.filter.h,
                             "the category rail is drawn over the filter");
  micronotes::tests::require(layout.values.y >= layout.sectionHeader.y + layout.sectionHeader.h,
                             "the rows are drawn over the section heading");
  micronotes::tests::require(layout.values.y + layout.values.h <= layout.footer.y + 1.0f,
                             "the rows run under the footer");
  // The two columns of the body do not overlap either.
  micronotes::tests::require(layout.values.x >= layout.rail.x + layout.rail.w,
                             "the values column is drawn over the category rail");
}

MICRONOTES_TEST(about_is_one_column_with_no_category_rail) {
  const auto layout = micronotes::ui::settingsLayout(1400.0f, 900.0f, SettingsMode::About);
  // A rail of categories over a list nobody groups would be a rail with one
  // entry in it.
  MICRONOTES_REQUIRE(layout.rail.w == 0.0f);
  MICRONOTES_REQUIRE(layout.sectionHeader.h == 0.0f);
  MICRONOTES_REQUIRE(layout.values.x == layout.panel.x);
  MICRONOTES_REQUIRE(layout.values.w == layout.panel.w);
}

MICRONOTES_TEST(both_modes_keep_the_filter_and_the_footer) {
  // The filter because About is the key reference and looking a key up is the
  // point of it; the footer because both modes have a count worth printing.
  for(const auto mode : {SettingsMode::Settings, SettingsMode::About}) {
    const auto layout = micronotes::ui::settingsLayout(1400.0f, 900.0f, mode);
    micronotes::tests::require(layout.filter.w > 0.0f && layout.filter.h > 0.0f,
                               "a mode was laid out with no filter");
    micronotes::tests::require(layout.footer.w > 0.0f && layout.footer.h > 0.0f,
                               "a mode was laid out with no footer");
    micronotes::tests::require(layout.values.h > 0.0f, "a mode was laid out with no room for content");
  }
}
