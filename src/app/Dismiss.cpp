#include "app/Dismiss.h"

#include "app/Notes.h"

namespace micronotes::app {

Dismissed dismissOne(UiRuntime& ui) {
  // A query owns the row list while it runs, so it is the innermost thing on
  // screen and the first to come off. Asked of the field rather than of the
  // focus: a query is still narrowing the sidebar after the reader has clicked
  // into a result, and Esc from there should clear the query rather than fall
  // straight through to the page.
  if(!ui.search.empty()) {
    ui.search.reset();
    ui.state.setSearch("", ui.searchScope);
    return Dismissed::Search;
  }
  if(!ui.find.empty()) {
    ui.find.reset();
    return Dismissed::Find;
  }
  if(ui.creatingFolder) {
    ui.creatingFolder = false;
    return Dismissed::FolderName;
  }
  // The tag filter, which is the one that had no way out at all. It sits
  // *under* a query -- choosing a tag clears the query, and typing a query
  // leaves the tag in force -- so a reader who did both gets two presses.
  if(clearTagFilter(ui)) return Dismissed::TagFilter;
  // Last, because it is a selection inside the page rather than a filter over
  // the library: with nothing narrowed, Esc belongs to whatever has focus.
  if(ui.blockSelectActive) {
    ui.clearBlockSelection();
    return Dismissed::BlockSelection;
  }
  return Dismissed::Nothing;
}

}
