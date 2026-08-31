#include "TestSupport.h"

#include "ui/AppState.h"
#include "ui/Settings.h"
#include "ui/Theme.h"

#include <filesystem>
#include <fstream>
#include <string>

using micronotes::ui::AppState;
using micronotes::ui::PageWidth;
using micronotes::ui::TextSize;

namespace {

// Every setting is global, so a test that changes one puts it back rather than
// deciding what the next test starts from.
struct ScopedAppearance {
  ScopedAppearance()
    : size(micronotes::ui::textSize()),
      width(micronotes::ui::pageWidth()),
      mode(micronotes::ui::themeMode()) {}

  ~ScopedAppearance() {
    micronotes::ui::setTextSize(size);
    micronotes::ui::setPageWidth(width);
    micronotes::ui::setThemeMode(mode);
  }

  TextSize size;
  PageWidth width;
  micronotes::ui::ThemeMode mode;
};

std::filesystem::path scratchDir(const std::string& name) {
  auto dir = std::filesystem::temp_directory_path() / ("micronotes-settings-" + name);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

}

MICRONOTES_TEST(settings_names_round_trip) {
  for(const auto size : {TextSize::Small, TextSize::Medium, TextSize::Large}) {
    MICRONOTES_REQUIRE(micronotes::ui::textSizeFromName(micronotes::ui::textSizeName(size)) == size);
  }
  for(const auto width : {PageWidth::Narrow, PageWidth::Medium, PageWidth::Wide}) {
    MICRONOTES_REQUIRE(micronotes::ui::pageWidthFromName(micronotes::ui::pageWidthName(width)) == width);
  }
  // A state file written by hand, or by a newer version, must land somewhere
  // readable rather than on a zero size or a zero-width column.
  MICRONOTES_REQUIRE(micronotes::ui::textSizeFromName("enormous") == TextSize::Medium);
  MICRONOTES_REQUIRE(micronotes::ui::pageWidthFromName("") == PageWidth::Medium);
}

MICRONOTES_TEST(settings_steps_are_ordered_and_positive) {
  ScopedAppearance restore;
  micronotes::ui::setTextSize(TextSize::Small);
  const float small = micronotes::ui::textScale();
  micronotes::ui::setTextSize(TextSize::Medium);
  const float medium = micronotes::ui::textScale();
  micronotes::ui::setTextSize(TextSize::Large);
  const float large = micronotes::ui::textScale();
  MICRONOTES_REQUIRE(small > 0.0f && small < medium && medium < large);

  micronotes::ui::setPageWidth(PageWidth::Narrow);
  const float narrow = micronotes::ui::pageWidthPx();
  micronotes::ui::setPageWidth(PageWidth::Wide);
  const float wide = micronotes::ui::pageWidthPx();
  MICRONOTES_REQUIRE(narrow > 0.0f && narrow < wide);
}

MICRONOTES_TEST(ui_state_carries_appearance_settings) {
  ScopedAppearance restore;
  const auto dir = scratchDir("appearance");
  const auto statePath = dir / "ui.state";

  AppState saved;
  micronotes::ui::setTextSize(TextSize::Large);
  micronotes::ui::setPageWidth(PageWidth::Narrow);
  MICRONOTES_REQUIRE(saved.saveUiState(statePath));

  micronotes::ui::setTextSize(TextSize::Small);
  micronotes::ui::setPageWidth(PageWidth::Wide);

  AppState loaded;
  MICRONOTES_REQUIRE(loaded.loadUiState(statePath));
  MICRONOTES_REQUIRE(micronotes::ui::textSize() == TextSize::Large);
  MICRONOTES_REQUIRE(micronotes::ui::pageWidth() == PageWidth::Narrow);

  std::filesystem::remove_all(dir);
}

MICRONOTES_TEST(ui_state_load_does_not_inherit_the_previous_library) {
  const auto dir = scratchDir("inherit");
  const auto statePath = dir / "ui.state";

  AppState state;
  state.shell().favorites.push_back("note-a");
  state.selectNote("note-a");
  MICRONOTES_REQUIRE(state.saveUiState(statePath));
  MICRONOTES_REQUIRE(state.loadUiState(statePath));
  MICRONOTES_REQUIRE(state.shell().favorites.size() == 1);

  // A library with no state file of its own opens empty. Without this, opening
  // one from the settings dialog would show the favorites - and the open note -
  // of the library just left.
  MICRONOTES_REQUIRE(!state.loadUiState(dir / "missing.state"));
  MICRONOTES_REQUIRE(state.shell().favorites.empty());
  MICRONOTES_REQUIRE(state.selection().noteId.empty());

  std::filesystem::remove_all(dir);
}
