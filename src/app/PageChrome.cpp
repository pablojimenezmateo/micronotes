#include "app/PageChrome.h"

#include "app/Desktop.h"
#include "app/Notes.h"
#include "app/WikiLinks.h"
#include "core/attachments/AttachmentService.h"
#include "doc/BlockScan.h"
#include "doc/LinkTarget.h"

#include <exception>
#include <filesystem>
#include <vector>

namespace micronotes::app {

std::optional<std::string> codeUnderCopyButton(const PageView& page, std::string_view source,
                                               float x, float y) {
  const auto blockStart = page.copyButtonAt(x, y);
  if(!blockStart) return std::nullopt;
  const auto& blocks = page.document().blocks();
  const auto& block = blocks[doc::blockIndexAt(blocks, *blockStart)];
  return std::string {source.substr(block.contentStart(), block.contentEnd() - block.contentStart())};
}


const LinkRegion* linkAt(const UiRuntime& ui, float x, float y) {
  for(const auto& link : ui.linkRegions) {
    if(ui::contains(link.rect, x, y)) return &link;
  }
  return nullptr;
}

bool pointOnLink(const UiRuntime& ui, float x, float y) {
  return linkAt(ui, x, y) != nullptr;
}

bool followLinkAt(UiRuntime& ui, float x, float y) {
  if(const LinkRegion* found = linkAt(ui, x, y)) {
    const auto target = found->target;
    const bool wiki = found->wiki;
    if(wiki) {
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
    if(doc::isRemoteTarget(target)) {
      ui.status = openWithDesktop(target) ? "Opened " + target : "Open failed";
      return true;
    }
    if(!ui.state.hasLibrary()) {
      ui.status = "No library for local link";
      return true;
    }
    const auto relative = filePart.empty() ? target : filePart;
    // Which note this path names, if any. Resolved once, and used for both
    // questions below -- is it the note already open, and is it a note at all.
    const library::NoteListItem* note = noteAtLinkTarget(ui, relative);
    // `note.md#heading` pointing at the note already open is a jump, not an
    // open: the file is on screen and scrolling to it is what was meant.
    //
    // By identity rather than by file name. This compared `path.filename()`
    // against the target's, so in a library with an `index.md` under two
    // folders a link from one to the other scrolled the note you were already
    // reading to a heading of its own instead of opening the note asked for.
    if(!anchorPart.empty() && note && note->id == ui.state.selection().noteId) {
      ui.status = jumpToAnchor(ui, anchorPart) ? "Jumped to " + anchorPart
                                              : "Anchor not found: " + anchorPart;
      return true;
    }
    // A link to another note in the library opens that note, in a tab, like
    // every other route to a note. It used to fall through to the desktop
    // below, so `[the plan](work/project-plan.md)` -- the form every other
    // Markdown tool writes, and the form a file imported from elsewhere already
    // contains -- launched whatever the system associates with `.md` and left
    // the reader watching a second application open a file out of the library
    // they were already reading. `[[wikilinks]]` navigated and these did not,
    // which is what made the viewer's links look inert.
    if(note) {
      const auto title = note->title;
      selectNoteById(ui, note->id);
      ui.status = "Opened " + title;
      // The fragment comes along: `other.md#a-section` names both a note and a
      // place in it, and arriving at the top would drop half the link. Queued
      // rather than jumped, because the page is still holding the note we came
      // from until the next frame lays this one out -- see `queueAnchorJump`.
      if(!anchorPart.empty()) queueAnchorJump(ui, anchorPart);
      return true;
    }
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
