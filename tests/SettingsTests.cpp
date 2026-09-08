#include "TestSupport.h"

#include "ui/AppState.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/UiStateFile.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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
  state.editWorkspace().favorites.push_back("note-a");
  state.selectNote("note-a");
  MICRONOTES_REQUIRE(state.saveUiState(statePath));
  MICRONOTES_REQUIRE(state.loadUiState(statePath));
  MICRONOTES_REQUIRE(state.workspace().favorites.size() == 1);

  // A library with no state file of its own opens empty. Without this, opening
  // one from the settings dialog would show the favorites - and the open note -
  // of the library just left.
  MICRONOTES_REQUIRE(!state.loadUiState(dir / "missing.state"));
  MICRONOTES_REQUIRE(state.workspace().favorites.empty());
  MICRONOTES_REQUIRE(state.selection().noteId.empty());

  std::filesystem::remove_all(dir);
}

// Tag colours and shut bands are view preferences, so they live in the ui state
// beside the library and never touch a note.
//
// The point of them being *there* rather than in front matter: what colour
// somebody finds `work` easiest to spot, and whether they keep TAGS shut
// because their library has sixty of them, are facts about a reader and not
// about a note. Writing either into the notes would make a preference a
// library-wide edit -- and would put it in everyone's git history.
MICRONOTES_TEST(ui_state_carries_tag_colours_and_shut_bands) {
  using micronotes::ui::SidebarSection;
  const auto dir = scratchDir("tag-colours");
  const auto statePath = dir / "ui.state";

  AppState saved;
  saved.editWorkspace().tagColors.set("work", 7);
  saved.editWorkspace().tagColors.set("a tag with spaces", 2);
  // Deliberately not picked, so the file has nothing to say about it and the
  // load leaves it on its derived colour.
  saved.editWorkspace().setSectionCollapsed(SidebarSection::Tags, true);
  saved.editWorkspace().setSectionCollapsed(SidebarSection::Recent, true);
  MICRONOTES_REQUIRE(saved.saveUiState(statePath));

  AppState loaded;
  MICRONOTES_REQUIRE(loaded.loadUiState(statePath));
  MICRONOTES_REQUIRE(loaded.workspace().tagColors.swatchOf("work") == 7);
  // A name with a space in it survives, which is why the swatch index is
  // written first: the tag is the only field that could hold the separator.
  MICRONOTES_REQUIRE(loaded.workspace().tagColors.swatchOf("a tag with spaces") == 2);
  MICRONOTES_REQUIRE(loaded.workspace().tagColors.choices().size() == 2);
  MICRONOTES_REQUIRE(!loaded.workspace().tagColors.picked("personal"));
  MICRONOTES_REQUIRE(loaded.workspace().sectionCollapsed(SidebarSection::Tags));
  MICRONOTES_REQUIRE(loaded.workspace().sectionCollapsed(SidebarSection::Recent));
  MICRONOTES_REQUIRE(!loaded.workspace().sectionCollapsed(SidebarSection::Notebooks));
  MICRONOTES_REQUIRE(!loaded.workspace().sectionCollapsed(SidebarSection::Favorites));

  // Only shut bands are written, so the common state -- all four open -- costs
  // nothing, and a file from before bands existed reads as all four open,
  // which is the arrangement it was written under.
  AppState allOpen;
  MICRONOTES_REQUIRE(allOpen.saveUiState(dir / "open.state"));
  std::ifstream in(dir / "open.state");
  const std::string text {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  MICRONOTES_REQUIRE(text.find("collapsed=") == std::string::npos);
  MICRONOTES_REQUIRE(text.find("tag_color=") == std::string::npos);

  // And a library with no state file of its own does not inherit the last
  // one's colours or its shut bands, for the reason it does not inherit its
  // favorites: this is "the view state is now whatever that file says".
  MICRONOTES_REQUIRE(!loaded.loadUiState(dir / "missing.state"));
  MICRONOTES_REQUIRE(loaded.workspace().tagColors.choices().empty());
  MICRONOTES_REQUIRE(!loaded.workspace().sectionCollapsed(SidebarSection::Tags));

  std::filesystem::remove_all(dir);
}

// A hand-edited state file must not be able to repaint a tag by accident.
MICRONOTES_TEST(ui_state_drops_a_tag_colour_line_it_cannot_read) {
  const auto dir = scratchDir("tag-colours-bad");
  const auto statePath = dir / "ui.state";
  {
    std::ofstream out(statePath, std::ios::binary | std::ios::trunc);
    out << "tag_color=notanumber|work\n"     // unparseable index
        << "tag_color=|orphan\n"             // no index at all
        << "tag_color=3\n"                   // no tag
        << "tag_color=4|\n"                  // no tag name
        << "tag_color=2|keeper\n"            // and one good line
        << "collapsed=nosuchsection\n";
  }

  AppState state;
  MICRONOTES_REQUIRE(state.loadUiState(statePath));
  // The good line took, and every bad one was dropped rather than defaulted --
  // silently painting `work` swatch 0 would hide the fact that the file and the
  // reader disagree.
  MICRONOTES_REQUIRE(state.workspace().tagColors.swatchOf("keeper") == 2);
  MICRONOTES_REQUIRE(state.workspace().tagColors.choices().size() == 1);
  MICRONOTES_REQUIRE(!state.workspace().tagColors.picked("work"));
  MICRONOTES_REQUIRE(!state.workspace().tagColors.picked("orphan"));
  // An unknown section name shuts nothing, rather than shutting the first one.
  std::size_t count = 0;
  const auto* sections = micronotes::ui::sidebarSections(&count);
  for(std::size_t i = 0; i < count; ++i) {
    MICRONOTES_REQUIRE(!state.workspace().sectionCollapsed(sections[i]));
  }

  std::filesystem::remove_all(dir);
}

// The names in the file are the names the file is read back with.
MICRONOTES_TEST(sidebar_section_names_round_trip) {
  std::size_t count = 0;
  const auto* sections = micronotes::ui::sidebarSections(&count);
  MICRONOTES_REQUIRE(count == 4);
  std::set<std::string> names;
  for(std::size_t i = 0; i < count; ++i) {
    const auto name = micronotes::ui::sidebarSectionName(sections[i]);
    MICRONOTES_REQUIRE(!name.empty());
    names.insert(std::string(name));
  }
  // Four distinct names, or two bands would share a line in the state file and
  // shutting one would shut the other.
  MICRONOTES_REQUIRE(names.size() == count);
}

// The compatibility story the format promises: keys are added, never
// redefined, so a file written before tabs existed still opens the note it
// names.
//
// This is the half of `ui/UiStateFile.h` that no round trip can reach --
// writing and reading with the same binary never produces a file without
// `tab=` lines -- so it has to be written out by hand. It is also the half most
// likely to be broken by a change to the format, because the code that honours
// it looks like dead weight.
MICRONOTES_TEST(ui_state_opens_a_file_written_before_tabs_existed) {
  const auto dir = scratchDir("ui-state-legacy");
  const auto statePath = dir / "ui.state";
  {
    std::ofstream out(statePath);
    out << "pane=1\n";           // reading
    out << "note=some-note-id\n";
    out << "sidebar=240\n";
  }

  micronotes::ui::WorkspaceModel workspace;
  micronotes::ui::UiSelection selection;
  MICRONOTES_REQUIRE(micronotes::ui::readUiState(statePath, workspace, selection));

  // One tab, synthesised from the two legacy keys rather than an empty session.
  MICRONOTES_REQUIRE(workspace.tabs.size() == 1);
  MICRONOTES_REQUIRE(workspace.tabs[0].noteId == "some-note-id");
  MICRONOTES_REQUIRE(workspace.tabs[0].paneMode == micronotes::ui::PaneMode::Viewer);
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  MICRONOTES_REQUIRE(selection.noteId == "some-note-id");
  // And a key it has never heard of does not stop it reading the rest.
  MICRONOTES_REQUIRE(workspace.sidebarWidth == 240.0f);
}

// The other direction: a file with `tab=` lines was written by a version that
// has tabs, so its legacy `pane=` echo must not override the active tab's own
// pane. Getting this wrong makes every tab open in whatever pane the *first*
// session used.
MICRONOTES_TEST(ui_state_ignores_the_legacy_pane_when_tabs_are_present) {
  const auto dir = scratchDir("ui-state-legacy-echo");
  const auto statePath = dir / "ui.state";
  {
    std::ofstream out(statePath);
    out << "pane=1\n";                   // the echo: reading
    out << "tab=3|0|first\n";            // live
    out << "tab=0|1|second\n";           // raw, pinned
    out << "active_tab=1\n";
    out << "note=first\n";               // the legacy echo of the open note
  }

  micronotes::ui::WorkspaceModel workspace;
  micronotes::ui::UiSelection selection;
  MICRONOTES_REQUIRE(micronotes::ui::readUiState(statePath, workspace, selection));

  MICRONOTES_REQUIRE(workspace.tabs.size() == 2);
  MICRONOTES_REQUIRE(workspace.tabs[0].paneMode == micronotes::ui::PaneMode::Live);
  MICRONOTES_REQUIRE(workspace.tabs[1].paneMode == micronotes::ui::PaneMode::Editor);
  MICRONOTES_REQUIRE(workspace.tabs[1].pinned);
  MICRONOTES_REQUIRE(workspace.activeTab == 1);
  // The active tab is what is open, whatever the legacy `note=` line said.
  MICRONOTES_REQUIRE(selection.noteId == "second");
}

// An out-of-range active tab in a hand-edited or truncated file must clamp
// rather than index past the end.
MICRONOTES_TEST(ui_state_clamps_an_active_tab_past_the_end) {
  const auto dir = scratchDir("ui-state-clamp");
  const auto statePath = dir / "ui.state";
  {
    std::ofstream out(statePath);
    out << "tab=3|0|only\n";
    out << "active_tab=9\n";
  }
  micronotes::ui::WorkspaceModel workspace;
  micronotes::ui::UiSelection selection;
  MICRONOTES_REQUIRE(micronotes::ui::readUiState(statePath, workspace, selection));
  MICRONOTES_REQUIRE(workspace.activeTab == 0);
  MICRONOTES_REQUIRE(selection.noteId == "only");
}
