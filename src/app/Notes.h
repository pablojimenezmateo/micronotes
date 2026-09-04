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

// Brings the editor back in line with a note whose file has changed underneath
// it, and reports whether it reloaded.
//
// A clean buffer is replaced by what is on disk -- which is what "a file that
// changed on disk should be reloaded" means, and what makes editing a note in
// another program, or pulling a branch, or letting a sync daemon deliver a
// change, work instead of quietly losing to the next autosave. A dirty buffer
// is left exactly as it is: the save path is what resolves that case, and it
// resolves it by keeping both versions rather than choosing one.
bool reloadSelectedIfChangedOnDisk(UiRuntime& ui);

// Catches the library up after something outside micronotes has been at it:
// the index, the note list, the wiki-link targets, and the open note's buffer.
//
// Called when the window regains focus, which is the moment a person who has
// been editing elsewhere comes back. It used to be four lines inside the event
// loop, and it was wrong in two ways: the whole thing was skipped whenever the
// buffer was dirty -- so an unsaved edit also froze the sidebar, the search
// index and the backlinks against a library that had moved on -- and it never
// touched the buffer, so the note on screen stayed at the bytes it was opened
// with and the next autosave wrote them back over whatever had arrived.
void rescanLibraryAfterExternalChange(UiRuntime& ui);

void createNote(UiRuntime& ui);
void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder);

// Writes the open note if it has unsaved changes. `quiet` suppresses the status
// line, for saves the user did not ask for rather than ones they did.
bool saveCurrent(UiRuntime& ui, bool quiet = false);

}
