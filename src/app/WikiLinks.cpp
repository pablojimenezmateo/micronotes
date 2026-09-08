#include "app/WikiLinks.h"

#include "app/Notes.h"
#include "app/Shell.h"

#include "ui/Outline.h"
#include "doc/LinkTarget.h"
#include "doc/WikiLink.h"
#include "library/WikiResolve.h"

#include <filesystem>
#include <string>
#include <vector>

namespace micronotes::app {

// Which notes a wikilink could be looking at, cached for as long as the library
// has not changed underneath it. Resolution runs once per link per layout, and
// listing every note in the library that many times per keystroke is the kind
// of cost that only shows up on somebody else's machine.
const std::vector<library::NoteListItem>& wikiCandidates(UiRuntime& ui) {
  return ui.wikiTargets.all(ui.state);
}

bool wikiLinkResolves(UiRuntime& ui, std::string_view target) {
  return library::resolveWikiLink(target, wikiCandidates(ui)) != std::string::npos;
}

void invalidateWikiNotes(UiRuntime& ui) {
  ui.wikiTargets.invalidate();
}

// Follows a `[[target]]`. A target that names nothing is not a mistake -- the
// link is very often written before the note is -- so it offers to create it
// rather than reporting a failure.
void openWikiLink(UiRuntime& ui, std::string_view target) {
  const auto split = doc::splitWikiTarget(target);
  if(split.note.empty()) return;
  const auto& notes = wikiCandidates(ui);
  const auto found = library::resolveWikiLink(target, notes);
  if(found != std::string::npos) {
    selectNoteById(ui, notes[found].id);
    if(!split.heading.empty()) {
      // A `#heading` is a place in the note, so arriving means arriving there.
      for(const auto& entry : ui::outlineOf(ui.editor.text())) {
        if(entry.text != split.heading) continue;
        ui.editor.moveCursor(entry.offset);
        ui.revealEditorCursor = true;
        break;
      }
    }
    return;
  }
  // Created beside the note that links to it, which is where someone writing
  // a link would have filed it by hand.
  std::filesystem::path folder = ui.state.selection().folder;
  if(const auto current = ui.state.findNote(ui.state.selection().noteId)) {
    folder = current->folder;
  }
  if(!saveCurrent(ui, true)) return;
  const auto created = ui.state.createNote(split.note, folder);
  if(!created) {
    ui.status = "Could not create " + split.note;
    return;
  }
  invalidateWikiNotes(ui);
  selectNoteById(ui, created->id);
  ui.status = "Created " + split.note;
}


// The wikilink picker. Opened by the second `[` of a `[[`, and filtered by what
// is typed after it, exactly as the slash menu is.
//
// `wikiStart` is the first `[`; committing replaces everything from there to
// the caret, so the two brackets the user typed are consumed by the link that
// takes their place. Escaping puts what was typed into the note instead of
// swallowing it, which is what makes the picker feel like part of typing.
void openWikiMenu(UiRuntime& ui, std::size_t wikiStart) {
  ui.wikiStart = wikiStart;
  ui.blockSelection.clear();
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "wiki-menu";
  overlay.title = "Link to a note";
  overlay.width = 400.0f;
  overlay.filterable = true;
  overlay.reportDismissal = true;
  overlay.placeholder = "Type a note title";
  overlay.hint = "Enter links, Esc keeps typing";
  const auto root = ui.state.libraryRoot();
  for(const auto& note : ui.state.allNotes()) {
    const auto folder = note.folder.generic_string();
    overlay.items.push_back({note.title, note.title, folder.empty() ? "" : folder, "", true, false});
  }
  ui.overlays.open(std::move(overlay));
}

// Replaces the `[[` the user typed with a finished link, or -- when they
// escaped -- with the two brackets and whatever they had typed after them, so
// that carrying on by hand is always an option.
void commitWikiMenu(UiRuntime& ui, const std::string& title, const std::string& typed) {
  const std::size_t caret = ui.editor.cursor();
  const std::size_t start = std::min(ui.wikiStart, caret);
  const std::string replacement = title.empty() ? "[[" + typed : "[[" + title + "]]";
  ui.editor.replaceRange(start, caret, replacement);
  // Landing the caret inside the brackets when nothing was chosen leaves the
  // link half-written and ready to be finished.
  if(title.empty()) ui.editor.moveCursor(start + replacement.size());
  ui.markEdited();
  ui.revealEditorCursor = true;
  invalidateWikiNotes(ui);
}


bool jumpToAnchor(UiRuntime& ui, std::string_view anchor) {
  const auto slug = doc::headingAnchor(anchor);
  const bool live = ui.state.workspace().paneMode() == ui::PaneMode::Live;
  PageView& page = live ? ui.livePage : ui.readingPage;
  auto found = page.anchorScroll(slug);
  if(!found) found = page.anchorScroll(anchor);
  if(!found) return false;
  if(live) {
    ui.livePage.setScroll(*found);
    ui.focus = FocusArea::Editor;
  } else {
    ui.readingPage.setScroll(*found);
    ui.focus = FocusArea::Viewer;
  }
  return true;
}

void queueAnchorJump(UiRuntime& ui, std::string_view anchor) {
  ui.pendingAnchor.assign(anchor);
}

void applyQueuedAnchorJump(UiRuntime& ui) {
  if(ui.pendingAnchor.empty()) return;
  // Taken before it is spent, so a note whose anchor is not there does not ask
  // again on every subsequent frame -- which would also mean a reader who
  // scrolled away being dragged back.
  const std::string anchor = std::move(ui.pendingAnchor);
  ui.pendingAnchor.clear();
  // Reported rather than passed over. A link naming a heading that is not in
  // the note it points at is a broken link, and the note opening at the top
  // with no explanation looks like the anchor was ignored.
  if(!jumpToAnchor(ui, anchor)) ui.status = "Anchor not found: " + anchor;
}

}
