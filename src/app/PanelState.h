#pragma once

#include "CoreAliases.h"

#include "app/NoteCaches.h"
#include "app/Wheel.h"
#include "library/Library.h"
#include "ui/Memo.h"
#include "ui/Outline.h"
#include "ui/Rect.h"
#include "ui/WorkspaceModel.h"

#include <cstdint>
#include <string>
#include <vector>

// The panel to the right of the page: what the open note contains, rather than
// what the library contains.
//
// Nine loose fields on `UiRuntime` before this, and the grouping is not
// cosmetic -- it is what lets the panel's *rules* be code. All three of its
// views were rebuilt on every frame, and each was expensive in a different way.
// Measured over a real session on a 400-note library with a 200 KB note open,
// per frame: the outline scanned the whole note for its headings, 0.30 ms; the
// backlinks ran a SQLite query, 0.25 ms; and the tags *read the note back off
// disk*, 0.53 ms. The same frame drew the note itself in 0.16 ms -- so an idle
// frame spent two to three times as long on the panel beside the note as on
// the note.
//
// Two memos rather than one, because the three views do not depend on the same
// things. The outline is a function of the buffer, so it turns on the editor's
// revision and has to move while the reader types. Backlinks and tags come from
// the library -- the index and the note's front matter -- so they turn on the
// note and the library's revision, and typing must *not* move them.
namespace micronotes::app {

struct RightPanelState {
  // The two views that come from the library rather than from the buffer. One
  // memo for both, because both turn on the same key: asking for either is what
  // says the note or the library has moved.
  struct LibraryViews {
    std::vector<library::Backlink> backlinks;
    std::vector<std::string> tags;
  };

  ui::Memo<std::vector<ui::OutlineEntry>, std::uint64_t> outline;
  ui::Memo<LibraryViews, NoteRevision> library;

  // Where the panel drew each backlink and each tag last frame, so a click can
  // find what it named without laying the list out a second time.
  struct Row {
    ui::Rect rect;
    std::string id;   // a note id for a backlink, a tag for a tag
  };
  std::vector<Row> backlinkRows;
  std::vector<Row> tagRows;

  // The panel scrolls like every other list in the shell. It used to be the one
  // that did not: a note with more headings than the panel was tall simply
  // stopped listing them, with no scrollbar to say so and a wheel over it
  // scrolling the note behind instead.
  int scroll = 0;
  int maxScroll = 0;
  // Last frame's rect, so a wheel can be clamped to the same maximum the draw
  // computed without laying the panel out a second time.
  ui::Rect rect;
  WheelAccumulator wheel;

  // Starts the list at the top when the view or the note has changed under it,
  // and reports whether it did.
  //
  // A method rather than a reset at every site that could change either, for
  // the reason the memos above exist: the site that forgets leaves the panel
  // scrolled to an offset that belongs to something else. The fields it
  // compares are private so that there is no way to ask the question except
  // through the one that also answers it.
  bool rebaseScroll(ui::RightPanelView view, const std::string& noteId) {
    if(scrollKeyValid_ && scrollView_ == view && scrollNoteId_ == noteId) return false;
    scrollKeyValid_ = true;
    scrollView_ = view;
    scrollNoteId_ = noteId;
    scroll = 0;
    wheel.remainder = 0.0f;
    return true;
  }

private:
  // Which view of which note `scroll` belongs to. Two fields rather than one
  // joined key, because this is compared on every frame and a joined key would
  // build a string on every one of them to find out that nothing had moved.
  ui::RightPanelView scrollView_ = ui::RightPanelView::Outline;
  std::string scrollNoteId_;
  bool scrollKeyValid_ = false;
};

}
