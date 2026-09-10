#include "app/Dismiss.h"

#include "app/FindBar.h"
#include "app/Notes.h"

namespace micronotes::app {

Dismissed dismissOne(UiRuntime& ui) {
  // A query owns the row list while it runs, so it is the innermost thing on
  // screen and the first to come off. Asked of the field rather than of the
  // focus: a query is still narrowing the sidebar after the reader has clicked
  // into a result, and Esc from there should clear the query rather than fall
  // straight through to the page.
  if(!ui.fields.search.empty()) {
    ui.fields.search.reset();
    ui.state.setSearch("", ui.fields.searchScope);
    return Dismissed::Search;
  }
  // The bar rather than the field: a reader who clicked into the note to look
  // at a match still has the bar open over it with the highlights showing, and
  // Esc there means "put the search away". Asking the field would have found it
  // non-empty and cleared the text while leaving the bar on screen.
  if(ui.find.open) {
    closeFindInNote(ui);
    return Dismissed::Find;
  }
  if(ui.sidebar.creatingFolder) {
    ui.sidebar.creatingFolder = false;
    return Dismissed::FolderName;
  }
  // The tag filter, which is the one that had no way out at all. It sits
  // *under* a query -- choosing a tag clears the query, and typing a query
  // leaves the tag in force -- so a reader who did both gets two presses.
  if(clearTagFilter(ui)) return Dismissed::TagFilter;
  return Dismissed::Nothing;
}

}
