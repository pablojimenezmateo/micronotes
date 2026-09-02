#include "app/SessionState.h"

#include "CoreAliases.h"

#include "app/Notes.h"
#include "app/Shell.h"
#include "core/platform/DurableFile.h"
#include "core/platform/PathUtils.h"

#include <fstream>
#include <sstream>
#include <string>

namespace micronotes::app {

std::filesystem::path uiStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "ui.state";
}

std::filesystem::path foldStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "folds.state";
}

std::filesystem::path treeStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "tree.state";
}

std::filesystem::path libraryPathConfigPath() {
  return microcore::platform::resolveRuntimePaths().configDir / "library-path";
}

std::optional<std::filesystem::path> readConfiguredLibraryRoot() {
  std::ifstream in(libraryPathConfigPath());
  if(!in) return std::nullopt;
  std::string line;
  std::getline(in, line);
  if(line.empty()) return std::nullopt;
  return std::filesystem::path(line);
}

bool writeConfiguredLibraryRoot(const std::filesystem::path& root) {
  const auto configPath = libraryPathConfigPath();
  return platform::writeFileDurably(configPath, root.generic_string() + "\n");
}

void persistLibraryState(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) return;
  ui.state.saveUiState(uiStatePath(ui.state.libraryRoot()));
  if(ui.folds.dirty()) ui.folds.save(foldStatePath(ui.state.libraryRoot()));
  if(ui.tree.dirty()) {
    platform::writeFileDurably(treeStatePath(ui.state.libraryRoot()), ui.tree.serialize());
  }
}

bool openLibraryRoot(UiRuntime& ui, const std::filesystem::path& root) {
  if(!ui.state.openOrCreateLibrary(root)) return false;
  ui.state.loadUiState(uiStatePath(ui.state.libraryRoot()));
  ui.folds.load(foldStatePath(ui.state.libraryRoot()));
  std::ostringstream treeBuffer;
  if(std::ifstream treeState(treeStatePath(ui.state.libraryRoot())); treeState) {
    treeBuffer << treeState.rdbuf();
  }
  ui.tree.load(treeBuffer.str());
  // Whatever was open last time still has to be reachable, so the folder
  // holding it is opened even if its parent was left collapsed.
  ui.tree.reveal(ui.state.selection().folder);
  ui.search.beginWith(ui.state.selection().search, false);
  ui.searchScope = ui.state.selection().searchScope;
  // The editor is emptied first: the previous library's note is gone, and a
  // library with nothing in it has no note to overwrite it with.
  ui.loadedNoteId.clear();
  ui.editor.setText("");
  ui.editor.markSaved();
  ui.sidebarScroll = 0;
  ui.editorScroll = 0;
  ui.viewerScroll = 0;
  loadSelectedIntoEditor(ui);
  if(ui.state.selection().noteId.empty()) selectNoteAt(ui, 0);
  return true;
}

}
