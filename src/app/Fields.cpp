#include "app/Fields.h"

#include "app/Notes.h"
#include "app/Shell.h"

#include <algorithm>
#include <string>

namespace micronotes::app {

editor::TextField* focusedField(UiRuntime& ui) {
  switch(ui.focus) {
    case FocusArea::Search: return &ui.search;
    case FocusArea::Find: return &ui.find;
    case FocusArea::TagEditor: return &ui.tag;
    case FocusArea::RenameNote: return &ui.rename;
    case FocusArea::RenameFolder: return &ui.folderRename;
    default: return nullptr;
  }
}

void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.search.text(), ui.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    updateFindStatus(ui);
  }
}

void updateFindStatus(UiRuntime& ui) {
  const std::string& needle = ui.find.text();
  if(needle.empty()) {
    ui.status = "Find in note";
    return;
  }
  std::size_t count = 0;
  std::size_t pos = ui.editor.text().find(needle);
  while(pos != std::string::npos) {
    ++count;
    pos = ui.editor.text().find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
  ui.status = std::to_string(count) + " matches in note";
}

// Focusing a search field is an action like any other, so the key and the
// palette row run the same code rather than two copies that drift.
void focusFindInNote(UiRuntime& ui) {
  ui.focus = FocusArea::Find;
  ui.find.editor.selectAll();
  updateFindStatus(ui);
}

void focusSearchAllNotes(UiRuntime& ui) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.focus = FocusArea::Search;
  ui.search.editor.selectAll();
  ui.state.setSearch(ui.search.text(), ui.searchScope);
  ui.status = "Search all notes";
}

}
