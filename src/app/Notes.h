#pragma once

#include <filesystem>
#include <string>

namespace micronotes::app {

struct UiRuntime;

// Opening, creating and saving the note on the page. Every one of these first
// writes whatever is already open, so switching notes can never lose an edit;
// they live together because that ordering is the thing they share.

// Opens the note at `index` in the current list.
void selectNoteAt(UiRuntime& ui, int index);

// Opens a note by id.
void selectNoteById(UiRuntime& ui, const std::string& noteId);

// Filters the library to one tag and opens the first note under it.
//
// Two surfaces do this -- the sidebar's TAGS section and the right panel's Tags
// view -- and both have to write the open note first, which is the reason this
// is here rather than at either of them.
void selectTag(UiRuntime& ui, const std::string& tag);

// Reloads the page from whatever the selection now names.
void loadSelectedIntoEditor(UiRuntime& ui);

void createNote(UiRuntime& ui);
void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder);

// Writes the open note if it has unsaved changes. `quiet` suppresses the status
// line, for saves the user did not ask for rather than ones they did.
bool saveCurrent(UiRuntime& ui, bool quiet = false);

}
