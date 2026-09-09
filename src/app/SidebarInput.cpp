#include "app/SidebarModel.h"

#include "AppPerfCounters.h"
#include "CoreAliases.h"
#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/Metrics.h"
#include "ui/RowBand.h"
#include "ui/ScrollList.h"
#include "ui/TreeModel.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

// What a press and the keyboard cursor do to the sidebar.
//
// Split from `SidebarModel.cpp`, which was 810 lines holding three things: the
// row list and its memo, this, and the queries over the rows a paint recorded.
// The row list is a pure function of the library and the panel; what is here is
// what a person does to it, and the two only meet through `ui.sidebar.rows`.
//
// That meeting is the rule worth keeping: nothing in this file rebuilds the
// rows or recomputes their geometry. It reads what the draw placed, which is
// why a click lands on the row that was painted rather than on one this file
// worked out for itself.

namespace micronotes::app {

SidebarDropTarget SidebarDropTarget::intoFolder(std::size_t row, std::filesystem::path folder) {
  SidebarDropTarget target;
  target.valid = true;
  target.row = row;
  target.folder = std::move(folder);
  return target;
}

SidebarDropTarget SidebarDropTarget::intoFilesDir(std::size_t row, std::filesystem::path filesDir) {
  SidebarDropTarget target;
  target.valid = true;
  target.row = row;
  target.filesDir = std::move(filesDir);
  return target;
}
using ui::Rect;
using ui::contains;

// Scrolls the cursor row into view using last frame's geometry, which is all
// that is needed to know whether it is off an edge and by how much.
void revealSidebarRow(UiRuntime& ui, std::size_t index) {
  if(index >= ui.sidebar.rows.size() || ui.sidebar.rect.h <= 0.0f) return;
  const Rect row = ui.sidebar.rows[index].rect;
  const float top = ui.sidebar.rect.y + 8.0f;
  const float bottom = ui.sidebar.rect.y + ui.sidebar.rect.h - 8.0f;
  if(row.y < top) ui.sidebar.list.scrollBy(-static_cast<int>(std::ceil(top - row.y)));
  else if(row.y + row.h > bottom) ui.sidebar.list.scrollBy(static_cast<int>(std::ceil(row.y + row.h - bottom)));
}

void moveTreeCursor(UiRuntime& ui, int delta) {
  if(ui.sidebar.rows.empty()) return;
  int index = std::clamp(ui.sidebar.cursor, 0, static_cast<int>(ui.sidebar.rows.size()) - 1) + delta;
  // A *caption* is drawn and nothing else, so the cursor steps over it. A band
  // is a control -- it shuts the rows under it -- so the cursor stops on one,
  // and Left/Right there shuts and opens it. Without that the collapsing was
  // reachable only with a pointer.
  //
  // Landing on a band is safe because `activateSidebarRow` ignores a `Cursor`
  // activation on one: arrowing past a band must not shut it.
  while(index >= 0 && index < static_cast<int>(ui.sidebar.rows.size()) &&
        ui.sidebar.rows[static_cast<std::size_t>(index)].kind == SidebarRow::Kind::SectionLabel &&
        !ui.sidebar.rows[static_cast<std::size_t>(index)].section) {
    index += delta;
  }
  if(index < 0 || index >= static_cast<int>(ui.sidebar.rows.size())) return;
  ui.sidebar.cursor = index;
  revealSidebarRow(ui, static_cast<std::size_t>(index));
  // Cursor, not Click: arrowing through the tree shows each note it passes
  // over, and must neither unfold the library nor open a tab per note.
  activateSidebarRow(ui, ui.sidebar.rows[static_cast<std::size_t>(index)], RowActivation::Cursor);
}

// Right opens a folder, or steps into it when it is already open; Left closes
// it, or jumps to its parent when there is nothing to close.
void expandTreeCursor(UiRuntime& ui, bool open) {
  if(ui.sidebar.cursor < 0 || ui.sidebar.cursor >= static_cast<int>(ui.sidebar.rows.size())) return;
  const SidebarRow row = ui.sidebar.rows[static_cast<std::size_t>(ui.sidebar.cursor)];
  // A band shuts and opens like a folder does, which is what it looks like:
  // both wear the same chevron and both hide the rows under them.
  if(row.kind == SidebarRow::Kind::SectionLabel && row.section) {
    if(open == !ui.state.workspace().sectionCollapsed(*row.section)) {
      moveTreeCursor(ui, open ? 1 : -1);
      return;
    }
    ui.state.editWorkspace().setSectionCollapsed(*row.section, !open);
    return;
  }
  const bool leaf = row.tree.kind == ui::TreeRowKind::Note || row.tree.kind == ui::TreeRowKind::File;
  if(row.kind != SidebarRow::Kind::Tree || leaf) {
    if(!open) moveTreeCursor(ui, -1);
    return;
  }
  if(open == row.tree.expanded) {
    moveTreeCursor(ui, open ? 1 : -1);
    return;
  }
  if(!row.tree.expandable) return;
  // A files directory is keyed by its own path, a notebook by its folder --
  // which for a notebook row *is* its own path. See `ui::TreeRow::file`.
  ui.sidebar.tree.setExpanded(row.tree.path(), open);
}

FocusArea chooseSidebarCursorRow(UiRuntime& ui) {
  if(ui.sidebar.cursor < 0 || ui.sidebar.cursor >= static_cast<int>(ui.sidebar.rows.size())) {
    return FocusArea::Editor;
  }
  const SidebarRow row = ui.sidebar.rows[static_cast<std::size_t>(ui.sidebar.cursor)];
  activateSidebarRow(ui, row, RowActivation::Click);
  // A tag and a band both change what the list is showing rather than opening
  // anything, so the reader stays in the list to see what happened.
  //
  // So does a companion row: a file opened elsewhere leaves nothing on the
  // page to focus, and a files directory only unfolds.
  const bool changedTheList = row.kind == SidebarRow::Kind::Tag ||
                              (row.kind == SidebarRow::Kind::SectionLabel && row.section) ||
                              (row.kind == SidebarRow::Kind::Tree &&
                               (row.tree.kind == ui::TreeRowKind::File ||
                                row.tree.kind == ui::TreeRowKind::FilesFolder));
  return changedTheList ? FocusArea::Folders : FocusArea::Editor;
}

void pressSidebarRow(UiRuntime& ui, const SidebarRow& row, float x, float y, Uint8 button) {
  // Innermost control first, all the way down. Each of these is drawn *on* the
  // row, so testing the row before any of them would mean the row swallowed
  // every one of its own controls.
  //
  // A tag dot at a note row's trailing edge is a control before it is
  // decoration: it says the note carries that tag, and clicking it filters by
  // it. It goes first because it is the smallest thing on the row.
  if(const auto tag = sidebarTagDotAt(ui, row, x, y)) {
    if(button == SDL_BUTTON_LEFT) selectTag(ui, *tag);
    else if(button == SDL_BUTTON_RIGHT) openTagMenu(ui, *tag, x, y);
    return;
  }
  // A folder's disclosure triangle opens it without making it the selection:
  // looking inside a notebook is not the same as switching to it. A band's
  // does not need a case of its own -- the whole band is its control, which
  // `activateSidebarRow` handles below.
  if(row.kind == SidebarRow::Kind::Tree && row.disclosure.w > 0.0f &&
     contains(row.disclosure, x, y) && button == SDL_BUTTON_LEFT) {
    ui.sidebar.tree.toggle(row.tree.path());
    return;
  }
  // A companion row's menu is about that row, so the row is remembered before
  // anything is opened -- there is no selection for a file to become.
  if(row.kind == SidebarRow::Kind::Tree && row.tree.isCompanion()) {
    if(button == SDL_BUTTON_RIGHT) {
      ui.sidebar.companionTarget = row.tree.file;
      if(row.tree.kind == ui::TreeRowKind::File) openFileMenu(ui, x, y);
      else openFilesFolderMenu(ui, x, y);
      return;
    }
    activateSidebarRow(ui, row, RowActivation::Click);
    if(button == SDL_BUTTON_LEFT) {
      ui.sidebar.drag.file = true;
      ui.sidebar.drag.filePath = row.tree.file;
    }
    return;
  }
  // On a tag's own row the whole row already means that tag, so the dot on it
  // is not a separate target; a right click is where its colour comes from.
  if(row.kind == SidebarRow::Kind::Tag && button == SDL_BUTTON_RIGHT) {
    openTagMenu(ui, row.tag, x, y);
    return;
  }
  activateSidebarRow(ui, row, RowActivation::Click);
  if(button == SDL_BUTTON_RIGHT) {
    if(row.kind == SidebarRow::Kind::SearchResult) openNoteMenu(ui, x, y);
    else if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::Note) openNoteMenu(ui, x, y);
    else if(row.kind == SidebarRow::Kind::Tree) openFolderMenu(ui, x, y);
    return;
  }
  // A left press on a tree row may turn into a drag, which is decided by
  // whether the pointer moves before it is let go.
  if(button == SDL_BUTTON_LEFT && row.kind == SidebarRow::Kind::Tree) {
    if(row.tree.kind == ui::TreeRowKind::Note) {
      ui.sidebar.drag.note = true;
      ui.sidebar.drag.noteId = row.tree.noteId;
    } else if(!row.tree.folder.empty()) {
      ui.sidebar.drag.folder = true;
      ui.sidebar.drag.folderPath = row.tree.folder;
    }
  }
}

void activateSidebarRow(UiRuntime& ui, const SidebarRow& row, RowActivation how) {
  // A band is a control, and the whole band is the control -- not just the
  // chevron on it. That is what every collapsible heading does, and on a 12px
  // triangle it is the difference between a target and a game of darts.
  //
  // Click only. The keyboard cursor steps over section labels rather than
  // landing on them, so a `Cursor` activation here would mean a band shutting
  // as the reader arrowed past it.
  if(row.kind == SidebarRow::Kind::SectionLabel) {
    if(how == RowActivation::Click && row.section) ui.state.editWorkspace().toggleSection(*row.section);
    return;
  }
  // Passing over a note takes over the tab showing; asking for one gives it a
  // tab of its own.
  const auto policy = how == RowActivation::Click ? ui::TabPolicy::NewTab : ui::TabPolicy::Reuse;
  if(row.kind == SidebarRow::Kind::SearchResult) {
    selectNoteById(ui, row.noteId, policy);
    return;
  }
  if(row.kind == SidebarRow::Kind::Tag) {
    // Click only, for the reason a folder is not unfolded by being passed over
    // and a note passed over does not get a tab of its own: choosing a tag
    // replaces the **entire row list** with the notes carrying it, so a walk
    // that applied one would destroy the list it was walking -- the cursor's
    // index would then point into a different list, and the next Down would
    // land somewhere unrelated. Holding Down through the TAGS band did exactly
    // that, and with the toggle on `selectTag` a second pass over the same tag
    // turned the filter off again.
    if(how == RowActivation::Click) selectTag(ui, row.tag);
    return;
  }
  if(row.kind != SidebarRow::Kind::Tree) return;
  // A companion file opens in whatever the desktop opens it with, and only when
  // asked for: a cursor passing over it must not launch a program per row. It
  // is never the selection -- there is no page for it -- so the note on screen
  // and the notebook in the breadcrumb stay exactly where they were.
  if(row.tree.kind == ui::TreeRowKind::File) {
    if(how == RowActivation::Click) openCompanion(ui, row.tree.file);
    return;
  }
  // A files directory unfolds on a click and nothing else: it is not a notebook,
  // has no first note to open, and cannot be the current folder.
  if(row.tree.kind == ui::TreeRowKind::FilesFolder) {
    if(how == RowActivation::Click) ui.sidebar.tree.toggle(row.tree.file);
    return;
  }
  if(row.tree.kind == ui::TreeRowKind::Note) {
    selectNoteById(ui, row.tree.noteId, policy);
    // Opening a note moves the context to its folder *and* opens the tree onto
    // it, so the breadcrumb and the sidebar agree about where the note is. A
    // search owns the row list while it is running, so it is left alone.
    if(ui.fields.search.empty() && ui.state.selection().noteId == row.tree.noteId) {
      showFolder(ui, row.tree.folder);
    }
    return;
  }
  // A notebook has no note to open in a tab of its own; it opens the first note
  // in it, which is a move of the selection rather than opening anything new.
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectFolder(row.tree.folder);
  if(how == RowActivation::Click) ui.sidebar.tree.setExpanded(row.tree.folder, true);
  selectNoteAt(ui, 0);
}


SidebarDropTarget sidebarDropTargetAt(const UiRuntime& ui, float x, float y) {
  const auto index = sidebarRowAt(ui, x, y);
  if(!index) return {};
  const SidebarRow& row = ui.sidebar.rows[*index];
  // A row in a files area stands for a directory in it: the one a folder row
  // is, or the one a file sits in.
  if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::FilesFolder) {
    return SidebarDropTarget::intoFilesDir(*index, row.tree.file);
  }
  if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::File) {
    return SidebarDropTarget::intoFilesDir(*index, row.tree.folder);
  }
  // A note row stands for the folder holding it, so dropping between two notes
  // does the obvious thing rather than nothing.
  if(row.kind == SidebarRow::Kind::Tree) return SidebarDropTarget::intoFolder(*index, row.tree.folder);
  // And the band that names the root's contents stands for the root.
  if(row.kind == SidebarRow::Kind::SectionLabel && row.section &&
     *row.section == ui::SidebarSection::Notebooks) {
    return SidebarDropTarget::intoFolder(*index, {});
  }
  return {};
}

}
