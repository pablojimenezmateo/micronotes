#include "app/PageChrome.h"

#include "app/Notes.h"
#include "app/WikiLinks.h"
#include "core/attachments/AttachmentService.h"
#include "doc/BlockScan.h"
#include "ui/TextUtil.h"

#include <sys/types.h>
#include <unistd.h>

#include <exception>
#include <filesystem>
#include <vector>

namespace micronotes::app {
namespace {

// A desktop program, launched and let go of. `fork` + `execvp` rather than
// `system`, so a target with a space or a quote in its name is an argument
// rather than shell input.
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

}

std::optional<std::string> codeUnderCopyButton(const PageView& page, std::string_view source,
                                               float x, float y) {
  const auto blockStart = page.copyButtonAt(x, y);
  if(!blockStart) return std::nullopt;
  const auto& blocks = page.document().blocks();
  const auto& block = blocks[doc::blockIndexAt(blocks, *blockStart)];
  return std::string {source.substr(block.contentStart(), block.contentEnd() - block.contentStart())};
}


bool followLinkAt(UiRuntime& ui, float x, float y) {
  for(const auto& link : ui.linkRegions) {
    if(!ui::contains(link.rect, x, y)) continue;
    const auto target = link.target;
    if(link.wiki) {
      openWikiLink(ui, target);
      return true;
    }
    const auto hash = target.find('#');
    const auto filePart = hash == std::string::npos ? target : target.substr(0, hash);
    const auto anchorPart = hash == std::string::npos ? std::string() : target.substr(hash + 1);
    if(filePart.empty() && !anchorPart.empty()) {
      ui.status = jumpToAnchor(ui, anchorPart) ? "Jumped to " + anchorPart
                                               : "Anchor not found: " + anchorPart;
      return true;
    }
    if(ui::isRemoteTarget(target)) {
      ui.status = spawnDetached({"xdg-open", target}) ? "Opened " + target : "Open failed";
      return true;
    }
    // `note.md#heading` pointing at the note already open is a jump, not an
    // open: the file is on screen and scrolling to it is what was meant.
    if(!anchorPart.empty()) {
      const auto& note = ui.state.openNote();
      const bool sameNote = filePart.empty() ||
                            (!note.noteId.empty() && note.path.filename() ==
                                                       std::filesystem::path(filePart).filename());
      if(sameNote && jumpToAnchor(ui, anchorPart)) {
        ui.status = "Jumped to " + anchorPart;
        return true;
      }
    }
    if(!ui.state.hasLibrary()) {
      ui.status = "No library for local link";
      return true;
    }
    const auto relative = filePart.empty() ? target : filePart;
    try {
      attachments::AttachmentService service;
      const auto command = service.openCommand(ui.state.libraryRoot(), relative);
      ui.status = spawnDetached(command)
                    ? "Opened " + std::filesystem::path(relative).filename().string()
                    : "Open failed";
    } catch(const std::exception&) {
      ui.status = "Unsafe or unavailable link path";
    }
    return true;
  }
  return false;
}

}
