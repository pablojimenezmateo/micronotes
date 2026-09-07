#include "app/Desktop.h"

#include "app/Shell.h"

#include <SDL3/SDL.h>

#include <sys/types.h>
#include <unistd.h>

#include <iostream>

namespace micronotes::app {

bool setClipboardText(std::string_view value) {
  const std::string text {value};
  SDL_ClearError();
  const bool clipboardOk = SDL_SetClipboardText(text.c_str());
  const std::string clipboardError = SDL_GetError();
  SDL_ClearError();
  SDL_SetPrimarySelectionText(text.c_str());
  const bool clipboardHasText = SDL_HasClipboardText();
  const bool primaryHasText = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard set"
              << " bytes=" << text.size()
              << " clipboard_ok=" << clipboardOk
              << " clipboard_has_text=" << clipboardHasText
              << " primary_has_text=" << primaryHasText;
    if(!clipboardOk) std::cerr << " error=\"" << clipboardError << "\"";
    std::cerr << "\n";
  }
  return clipboardOk;
}

bool spawnDetached(const std::vector<std::string>& command) {
  if(command.empty()) return false;
  const pid_t pid = fork();
  if(pid < 0) return false;
  if(pid == 0) {
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for(const auto& part : command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  return true;
}

bool openWithDesktop(std::string_view target) {
  if(target.empty()) return false;
  return spawnDetached({"xdg-open", std::string(target)});
}

bool revealInFileManager(const std::filesystem::path& path) {
  if(path.empty()) return false;
  std::filesystem::path containing = path.parent_path();
  // A path with no parent is not something a note in a library produces, but
  // falling back to the path itself is a better answer than refusing.
  if(containing.empty()) containing = path;
  std::error_code ec;
  // Checked here rather than left to the launcher: a note deleted underneath
  // the app should report a failure the reader can see, not fork a process that
  // fails where nobody is looking.
  if(!std::filesystem::exists(containing, ec) || ec) return false;
  return spawnDetached({"xdg-open", containing.string()});
}

}
