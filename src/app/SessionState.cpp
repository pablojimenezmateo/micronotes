#include "app/SessionState.h"
#include "CoreAliases.h"

#include "app/Notes.h"
#include "core/attachments/AttachmentService.h"
#include "app/Shell.h"
#include "core/AppIdentity.h"
#include "core/platform/DurableFile.h"
#include "core/platform/PathUtils.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
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
  if(!ui.state.catalog().isOpen()) return;
  ui.state.saveUiState(uiStatePath(ui.state.catalog().root()));
  if(ui.folds.dirty()) ui.folds.save(foldStatePath(ui.state.catalog().root()));
  if(ui.sidebar.tree.dirty()) {
    platform::writeFileDurably(treeStatePath(ui.state.catalog().root()), ui.sidebar.tree.serialize());
  }
}

void installWatcherWake(UiRuntime& ui) {
  ui.watcher.setWake([] {
    // The only job is to make the loop run once. It carries no payload -- what
    // changed is read off the watcher on the main thread, where it can be acted
    // on -- and SDL_PushEvent is the one part of SDL safe to call from a
    // watcher's thread.
    SDL_Event wake {};
    wake.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake);
  });
}

bool attachFromCli(UiRuntime& ui, const std::filesystem::path& source) {
  if(source.empty()) return true;
  if(!ui.state.catalog().isOpen()) {
    std::cerr << "--attach requires --library\n";
    return false;
  }
  ui.state.loadUiState(uiStatePath(ui.state.catalog().root()));
  const auto selected = ui.state.openNote().read();
  if(!selected) {
    std::cerr << "--attach requires a selected note saved in UI state\n";
    return false;
  }
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.catalog().root(), selected->metadata.id, source);
    ui.editor.setText(selected->body);
    ui.editor.insert("\n" + link.markdown + "\n");
    ui.state.saveSelectedNote(ui.editor.text());
    std::cout << link.markdown << "\n";
    return true;
  } catch(const std::exception& error) {
    std::cerr << "attach failed: " << error.what() << "\n";
    return false;
  }
}

bool openLibraryRoot(UiRuntime& ui, const std::filesystem::path& root) {
  if(!ui.state.openOrCreateLibrary(root)) return false;
  // The state directory is excluded: the sqlite index, its write-ahead log and
  // every attachment live in there, so watching it would mean the app's own
  // index writes arriving back as "the library changed".
  //
  // A tree that cannot be watched -- the per-user inotify limit, a filesystem
  // that does not support it -- is not an error. The focus-gained refresh is
  // still there, and it is what micronotes had before this.
  ui.watcher.watch(ui.state.catalog().root(), {microcore::kAppDotDir});
  ui.state.loadUiState(uiStatePath(ui.state.catalog().root()));
  ui.folds.load(foldStatePath(ui.state.catalog().root()));
  std::ostringstream treeBuffer;
  if(std::ifstream treeState(treeStatePath(ui.state.catalog().root())); treeState) {
    treeBuffer << treeState.rdbuf();
  }
  ui.sidebar.tree.load(treeBuffer.str());
  // Whatever was open last time still has to be reachable, so the folder
  // holding it is opened even if its parent was left collapsed.
  ui.sidebar.tree.reveal(ui.state.selection().folder);
  ui.fields.search.beginWith(ui.state.selection().search, false);
  ui.fields.searchScope = ui.state.selection().searchScope;
  // The editor is emptied first: the previous library's note is gone, and a
  // library with nothing in it has no note to overwrite it with.
  ui.loadedNoteId.clear();
  ui.editor.setText("");
  ui.editor.markSaved();
  ui.sidebar.list.rebase();
  ui.raw.list.rebase();
  ui.livePage.setScroll(0);
  ui.readingPage.setScroll(0);
  loadSelectedIntoEditor(ui);
  if(ui.state.selection().noteId.empty()) selectNoteAt(ui, 0);
  return true;
}

}
