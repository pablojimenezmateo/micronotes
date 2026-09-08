#include "app/Startup.h"

#include "app/BlockMenus.h"
#include "app/Commands.h"
#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/SessionState.h"
#include "app/SettingsDialog.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "ui/WorkspaceModel.h"

#include <iostream>
#include <string>
#include <string_view>

namespace micronotes::app {
namespace {

// Comma-separated: each title opens in a tab of its own, so a capture can show
// a strip without having to know any note's id. It no longer has to ask for
// that -- opening a note is opening a tab now -- so the `first` flag this used
// to keep is gone.
void openTitles(UiRuntime& ui, std::string_view titles) {
  while(!titles.empty()) {
    const auto comma = titles.find(',');
    const auto title = titles.substr(0, comma);
    for(const auto& note : ui.state.allNotes()) {
      if(note.title.find(title) == std::string::npos) continue;
      ui.state.selectNote(note.id);
      showFolder(ui, note.folder);
      loadSelectedIntoEditor(ui);
      break;
    }
    if(comma == std::string_view::npos) break;
    titles.remove_prefix(comma + 1);
  }
}

ui::PaneMode paneModeFromOption(int value) {
  switch(value) {
    case 0: return ui::PaneMode::Editor;
    case 1: return ui::PaneMode::Viewer;
    case 2: return ui::PaneMode::Split;
    default: return ui::PaneMode::Live;
  }
}

}

bool applyStartupOptions(UiRuntime& ui, ApplicationOptions& options) {
  if(options.configuredLibraryRoot) {
    if(!writeConfiguredLibraryRoot(*options.configuredLibraryRoot)) {
      std::cerr << "Failed to write library path config: " << *options.configuredLibraryRoot << "\n";
      return false;
    }
  }
  // The remembered library, when the command line named none.
  if(options.libraryRoot.empty()) {
    if(auto configured = readConfiguredLibraryRoot()) options.libraryRoot = *configured;
  }
  if(!options.libraryRoot.empty()) {
    if(!openLibraryRoot(ui, options.libraryRoot)) {
      std::cerr << "Failed to open library: " << options.libraryRoot << "\n";
      return false;
    }
  }
  if(!options.selectTitle.empty()) openTitles(ui, options.selectTitle);
  if(options.paneMode) ui.state.workspace().setPaneMode(paneModeFromOption(*options.paneMode));
  if(options.showSidebar) ui.state.workspace().sidebarVisible = *options.showSidebar;
  if(options.showRightPanel) ui.state.workspace().rightPanelVisible = *options.showRightPanel;
  if(!options.rightPanelView.empty()) {
    ui.state.workspace().rightPanelView = ui::rightPanelViewFromName(options.rightPanelView);
  }
  return attachFromCli(ui, options.attachPath);
}

void applyWindowOptions(UiRuntime& ui, const ApplicationOptions& options) {
  if(!options.searchQuery.empty()) {
    // Not selectAll: a capture wants the caret after the query, the way it sits
    // once the query has been typed.
    ui.fields.search.beginWith(options.searchQuery, false);
    ui.state.setSearch(options.searchQuery, ui.fields.searchScope);
    ui.focus = FocusArea::Search;
  }
  if(options.openOverlay.empty()) return;
  const auto& which = options.openOverlay;
  if(which == "rename") beginRename(ui);
  else if(which == "tags") beginTagEdit(ui);
  else if(which == "new-folder") beginFolderCreate(ui);
  else if(which == "note-menu") openNoteMenu(ui, 420.0f, 200.0f);
  else if(which == "folder-menu") openFolderMenu(ui, 60.0f, 160.0f);
  else if(which == "delete-note") openDeleteNoteConfirm(ui);
  else if(which == "settings") openSettings(ui);
  else if(which == "shortcuts") openShortcutHelp(ui);
  else if(which == "command-palette") openCommandPalette(ui);
  else if(which == "wiki-menu") openWikiMenu(ui, ui.editor.cursor());
  else if(which == "icon") openIconPicker(ui);
  else std::cerr << "unknown --open value: " << which << "\n";
}

}
