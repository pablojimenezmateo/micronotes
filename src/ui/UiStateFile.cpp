#include "ui/UiStateFile.h"

#include "ui/AppState.h"
#include "ui/Settings.h"
#include "ui/Theme.h"

#include "core/platform/DurableFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace micronotes::ui {
namespace {

// A whole-string parse: a partly-numeric value is a corrupt line, not a number
// with rubbish after it, and taking the prefix would let a hand-edited file
// half-apply.
int parseInt(const std::string& value, int fallback) {
  int result = fallback;
  const auto* first = value.data();
  const auto* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, result);
  if(ec != std::errc {} || ptr != last) return fallback;
  return result;
}

}

bool writeUiState(const std::filesystem::path& path, const WorkspaceModel& workspace,
                      const UiSelection& selection) {
  std::ostringstream out;
  // Written for a version that predates tabs: it reads the pane mode and the
  // open note from these two and gets a working single-tab session. Keys are
  // added rather than redefined, so an older binary keeps working too.
  out << "pane=" << static_cast<int>(workspace.paneMode()) << "\n";
  for(const auto& tab : workspace.tabs) {
    out << "tab=" << static_cast<int>(tab.paneMode) << "|" << (tab.pinned ? 1 : 0) << "|" << tab.noteId << "\n";
  }
  out << "active_tab=" << workspace.activeTab << "\n";
  // Rounded on the way out: an older binary parses these with from_chars into
  // an int, and "240.5" would make it fall back to its default instead.
  out << "sidebar=" << static_cast<int>(std::lround(workspace.sidebarWidth)) << "\n";
  out << "rightpanel=" << static_cast<int>(std::lround(workspace.rightPanelWidth)) << "\n";
  out << "panel_sidebar=" << (workspace.sidebarVisible ? 1 : 0) << "\n";
  out << "panel_right=" << (workspace.rightPanelVisible ? 1 : 0) << "\n";
  out << "right_view=" << rightPanelViewName(workspace.rightPanelView) << "\n";
  out << "folder=" << selection.folder.generic_string() << "\n";
  out << "tag=" << selection.tag << "\n";
  out << "note=" << selection.noteId << "\n";
  out << "search_scope=" << static_cast<int>(selection.searchScope) << "\n";
  out << "theme=" << themeModeName(themeMode()) << "\n";
  out << "text_size=" << textSizeName(textSize()) << "\n";
  out << "page_width=" << pageWidthName(pageWidth()) << "\n";
  // One line each rather than a delimited list: a note id never contains a
  // newline, and any other separator would eventually appear inside one.
  for(const auto& id : workspace.pinnedNotes) out << "pinned=" << id << "\n";
  for(const auto& id : workspace.recents) out << "recent=" << id << "\n";
  // Only the sections that are shut, so the common state -- all four open --
  // writes nothing and an older file reads as all four open, which is the
  // arrangement it was written under.
  std::size_t sectionCount = 0;
  const auto* sections = sidebarSections(&sectionCount);
  for(std::size_t i = 0; i < sectionCount; ++i) {
    if(workspace.sectionCollapsed(sections[i])) {
      out << "collapsed=" << sidebarSectionName(sections[i]) << "\n";
    }
  }
  // "<swatch index>|<tag>", the index first because a tag name is the only
  // field that could contain a separator. Only picked colours are written: a
  // tag following `defaultTagSwatch` has made no choice to persist, and
  // writing the default out would freeze it against a future palette.
  for(const auto& [tag, swatch] : workspace.tagColors.choices()) {
    out << "tag_color=" << swatch << "|" << tag << "\n";
  }
  return platform::writeFileDurably(path, out.str());
}

bool readUiState(const std::filesystem::path& path, WorkspaceModel& workspace,
                     UiSelection& selection) {
  // Cleared before the file is even opened: this is "the view state is now
  // whatever that file says", and a library with no state file of its own must
  // not inherit the pinned notes and the open note of the one before it.
  workspace.pinnedNotes.clear();
  workspace.recents.clear();
  workspace.tagColors.clearAll();
  workspace.collapsedSections = {};
  // A file written before panels could be hidden says nothing about them, and
  // the arrangement it was written under is the one the defaults describe.
  workspace.sidebarVisible = true;
  workspace.rightPanelVisible = false;
  workspace.tabs.clear();
  workspace.activeTab = 0;
  selection = {};
  std::ifstream in(path);
  if(!in) return false;
  std::string line;
  // A file written before tabs existed carries `pane=` and `note=` instead; one
  // tab is synthesised from them below rather than the session opening empty.
  std::optional<PaneMode> legacyPane;
  int activeTab = 0;
  while(std::getline(in, line)) {
    const auto eq = line.find('=');
    if(eq == std::string::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if(key == "pane") {
      const int mode = parseInt(value, static_cast<int>(PaneMode::Split));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Split)) {
        legacyPane = static_cast<PaneMode>(mode);
      }
    }
    else if(key == "tab") {
      // "<pane>|<pinned>|<note id>". The id is last and unsplit, because it is
      // the only field that could contain a separator.
      const auto firstBar = value.find('|');
      const auto secondBar = firstBar == std::string::npos ? std::string::npos : value.find('|', firstBar + 1);
      if(secondBar == std::string::npos) continue;
      NoteTab tab;
      const int mode = parseInt(value.substr(0, firstBar), static_cast<int>(PaneMode::Split));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Split)) {
        tab.paneMode = static_cast<PaneMode>(mode);
      }
      tab.pinned = parseInt(value.substr(firstBar + 1, secondBar - firstBar - 1), 0) != 0;
      tab.noteId = value.substr(secondBar + 1);
      if(!tab.noteId.empty()) workspace.tabs.push_back(std::move(tab));
    }
    else if(key == "active_tab") activeTab = parseInt(value, 0);
    else if(key == "sidebar") workspace.sidebarWidth = static_cast<float>(parseInt(value, static_cast<int>(workspace.sidebarWidth)));
    else if(key == "rightpanel") workspace.rightPanelWidth = static_cast<float>(parseInt(value, static_cast<int>(workspace.rightPanelWidth)));
    else if(key == "panel_sidebar") workspace.sidebarVisible = parseInt(value, 1) != 0;
    else if(key == "panel_right") workspace.rightPanelVisible = parseInt(value, 0) != 0;
    else if(key == "right_view") workspace.rightPanelView = rightPanelViewFromName(value);
    else if(key == "folder") selection.folder = value;
    else if(key == "tag") selection.tag = value;
    else if(key == "note") selection.noteId = value;
    else if(key == "theme") setThemeMode(themeModeFromName(value));
    else if(key == "text_size") setTextSize(textSizeFromName(value));
    else if(key == "page_width") setPageWidth(pageWidthFromName(value));
    else if(key == "pinned" && !value.empty()) workspace.pinnedNotes.push_back(value);
    // What the pinned list was called before it was called pinned, and what
    // the band that shows it was called before it was called PINNED. Read and
    // never written, so a library opened once comes back out under the new
    // spelling; without it the rename silently unpins every note somebody had
    // pinned, which is their data and not ours to drop.
    else if(key == "favorite" && !value.empty()) workspace.pinnedNotes.push_back(value);
    else if(key == "recent" && !value.empty()) workspace.recents.push_back(value);
    else if(key == "collapsed") {
      std::size_t sectionCount = 0;
      const auto* sections = sidebarSections(&sectionCount);
      for(std::size_t i = 0; i < sectionCount; ++i) {
        const bool named = sidebarSectionName(sections[i]) == value ||
                           (sections[i] == SidebarSection::Pinned && value == "favorites");
        if(named) workspace.setSectionCollapsed(sections[i], true);
      }
    }
    else if(key == "tag_color") {
      const auto bar = value.find('|');
      if(bar == std::string::npos || bar + 1 >= value.size()) continue;
      // A line whose index will not parse is dropped rather than defaulted to
      // swatch 0: a hand-edited file must not be able to silently repaint a
      // tag, and dropping it leaves that tag on its derived colour.
      const int swatch = parseInt(value.substr(0, bar), -1);
      if(swatch >= 0) workspace.tagColors.set(value.substr(bar + 1), swatch);
    }
    else if(key == "search_scope") {
      const int scope = parseInt(value, static_cast<int>(selection.searchScope));
      if(scope >= static_cast<int>(library::SearchScope::All) && scope <= static_cast<int>(library::SearchScope::Content)) {
        selection.searchScope = static_cast<library::SearchScope>(scope);
      }
    }
  }
  // A file with no `tab=` lines predates tabs: one is synthesised from the two
  // legacy keys rather than the session opening empty.
  //
  // Only then. A file that *has* tabs was written by a version that has them,
  // so its `pane=` is the legacy echo of the active tab's mode and must not be
  // applied to anything -- which is why `legacyPane` is read here and nowhere
  // else.
  if(workspace.tabs.empty() && !selection.noteId.empty()) {
    NoteTab tab;
    tab.noteId = selection.noteId;
    tab.paneMode = legacyPane.value_or(PaneMode::Split);
    workspace.tabs.push_back(std::move(tab));
  }
  workspace.activeTab = workspace.tabs.empty()
    ? 0
    : std::min(static_cast<std::size_t>(std::max(activeTab, 0)), workspace.tabs.size() - 1);
  // The active tab is what is open, whatever the legacy `note=` line said.
  if(const auto* tab = workspace.activeTab_()) selection.noteId = tab->noteId;
  return true;
}

}

