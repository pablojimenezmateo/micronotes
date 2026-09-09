#pragma once

#include "library/Organization.h"
#include "ui/Overlay.h"
#include "ui/Tabs.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// Opening, creating and saving the note on the page. Every one of these first
// writes whatever is already open, so switching notes can never lose an edit;
// they live together because that ordering is the thing they share.

// Opens the note at `index` in the current list.
void selectNoteAt(UiRuntime& ui, int index);

// Opens a note by id, in a tab of its own.
//
// Nothing in the app could open a second tab before this: `openNote` took a
// flag and honoured it, and exactly one caller in the whole app ever set it --
// one command palette. Every other route to a note went through here, which had
// no such parameter, so the sidebar, a search hit, a backlink and a wiki link
// all replaced the note you were reading.
//
// `TabPolicy::Reuse` is for the keyboard cursor walking the sidebar; see
// `ui::TabPolicy`.
void selectNoteById(UiRuntime& ui, const std::string& noteId,
                    ui::TabPolicy policy = ui::TabPolicy::NewTab);

// Makes `folder` the context *and* opens the tree down to it.
//
// The two halves are one action and were said separately at four call sites,
// which is what let the fifth say only half. A note opened from a flat list --
// RECENT, FAVORITES, a search hit -- selected its folder without opening it, so
// the note the user had just clicked appeared nowhere in the tree: the only row
// showing it was the flat one they clicked, sitting at the top level, outside
// the folder the breadcrumb had just started naming.
void showFolder(UiRuntime& ui, const std::filesystem::path& folder);

// Filters the library to one tag and opens the first note under it.
//
// Two surfaces do this -- the sidebar's TAGS section and the right panel's Tags
// view -- and both have to write the open note first, which is the reason this
// is here rather than at either of them.
void selectTag(UiRuntime& ui, const std::string& tag);

// Puts the tree back. A tag filter replaces the whole row list, so while one is
// in force there is no tree on screen to click your way out through -- and the
// tag row you came in by is not on screen either. Without this the only way
// back was to search for something and clear the search, which is a way out
// only if you already know the tree is still there.
//
// Reports whether it did anything, so Esc can fall through to whatever else it
// means when no filter is running.
bool clearTagFilter(UiRuntime& ui);

// Gives `tag` the swatch named by `swatch` -- the id a colour picker's result
// carries, so a decimal string -- and puts `tag` back on its derived colour.
//
// Beside `selectTag` because they are the same handful of verbs about the same
// thing, and all three have to write the ui state that carries the answer.
// Nothing here touches a note: what colour somebody finds a tag easiest to spot
// is a view preference, and writing it into every note carrying the tag would
// be a library-wide edit to record a preference. See `ui::TagColors`.
void setTagColor(UiRuntime& ui, std::string_view tag, std::string_view swatch);
void clearTagColor(UiRuntime& ui, std::string_view tag);

// Carries out a choice from the tag menu or the colour picker, and says whether
// it was one of theirs.
//
// Here rather than in the shell's overlay dispatcher because every one of the
// three answers is a verb on this page: filter, colour, un-colour. The tag
// itself travels in the result's `value` -- a result names the item chosen, and
// which tag it was about is the other half of the answer.
bool handleTagOverlayResult(UiRuntime& ui, const ui::OverlayResult& result);

// The two spellings of something's path, as the copy commands put them on the
// clipboard. Empty `absolute` means there is no such thing to name.
//
// Split from the commands so the part with the reasoning in it can be checked
// without a clipboard: writing to one needs an initialized video subsystem and
// a cooperating compositor, neither of which a unit test has, so a test that
// went through the command could only ever assert "the clipboard refused".
struct LibraryPaths {
  std::string absolute;
  // Relative to the library root. Empty when the thing is not under it, which
  // is the one case "relative path" has no honest answer for.
  std::string relative;
};

// Both spellings of an absolute path under the library root.
LibraryPaths libraryPathsFor(const UiRuntime& ui, const std::filesystem::path& absolute);
LibraryPaths notePathsFor(const UiRuntime& ui, std::string_view noteId);
// A notebook's, by its library-relative path. Empty names the library root,
// which has a path like any other folder and is worth being able to copy.
LibraryPaths folderPathsFor(const UiRuntime& ui, const std::filesystem::path& folder);

// Carries out "show on disk", "copy relative path" or "copy absolute path", and
// reports whether `command` was one of the three.
//
// `noteId` empty means the note on the page, which is what the palette and the
// menu bar mean by "the note"; the sidebar's and the tab strip's menus name
// theirs, because a right click there is about the row or the tab under the
// pointer rather than about whatever happens to be open. The folder form is
// about the notebook the sidebar tree has selected, which a right click on a
// tree row has just moved.
//
// A relative path is relative to the library root, which is what makes it worth
// having: it is the path that means the same thing to somebody else looking at
// the same library, and so the one that can go in a note, a commit message or a
// message to a colleague. It is refused rather than silently falling back to
// the absolute one when the thing somehow sits outside the library, because a
// "relative path" that is absolute is the wrong answer given confidently.
bool handleNotePathCommand(UiRuntime& ui, std::string_view command, std::string_view noteId);
bool handleFolderPathCommand(UiRuntime& ui, std::string_view command,
                             const std::filesystem::path& folder);

// The note a Markdown link's path names, or null when it names something that
// is not a note in this library.
//
// `[the plan](work/project-plan.md)` is the ordinary way to link one note to
// another -- it is what every other Markdown tool writes, and what a file
// dropped in from elsewhere already contains. Following one used to hand the
// path to `xdg-open`, so clicking it launched whatever the desktop associates
// with `.md` and the reader watched a second application open a file that was
// already sitting in the library they were reading. `[[wikilinks]]` navigated
// and these did not, which made the viewer's links look broken.
//
// Resolved relative to the *linking note's own folder* first and the library
// root second, which is the order a relative path means and the order the two
// disagree in: `work/project-plan.md` written inside `work/` could mean either.
// Escapes are decoded and a `#fragment` must already be off the string --
// `followLinkAt` splits it, because the anchor is its business and not this
// function's.
//
// A borrow into the standing note list, so it costs a comparison per note and
// no allocation. Null for a target outside the library, for a directory, and
// for a file that is not indexed as a note -- an attachment, an image, a PDF --
// each of which is still the desktop's job.
const library::NoteListItem* noteAtLinkTarget(UiRuntime& ui, std::string_view relative);

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

// Acts on whatever the library watcher has collected, and reports whether
// anything moved (so the caller knows to draw).
//
// Named paths are re-indexed one by one, which is why the watcher bothers to
// report them: on a library of any size, re-reading everything because one file
// changed is the difference between a keystroke and a stutter. A rescan request
// -- the kernel's queue overflowed, a directory was moved away, the tree is
// larger than the watch budget -- falls back to the full refresh, because those
// are the cases where the list of paths is no longer the list of what changed.
//
// micronotes' own writes arrive here too; nothing filters them out and nothing
// needs to. A re-index of a file whose stat still matches its row does no work,
// and a reload of a note whose signature still agrees does not happen. The
// echo suppression falls out of comparing what is on disk instead of keeping
// track of who wrote it.
bool applyWatchedChanges(UiRuntime& ui);

// Renaming, in two halves: the prompt, and what the answer does.
//
// A note is a file and its name is that file's name, so a title already taken
// by a note beside it cannot simply be granted. Nothing blocks the rename --
// the title is numbered (`TODO-2`) and the rename goes through -- and
// `saveRename` says so on the status line when the name it settled on is not
// the one that was typed.
void beginRename(UiRuntime& ui);
void saveRename(UiRuntime& ui);

// Creates a note called `title` in the selected folder and opens it. The name
// is asked for first -- see `beginNoteCreate` in `app/Prompts.h` -- except on
// the two paths that cannot stop to ask: a save with nothing selected, and a
// file dropped on a window with no note open.
bool createNote(UiRuntime& ui, const std::string& title);
// Chooses the notebook, then asks for the name.
void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder);

// Writes the open note if it has unsaved changes. `quiet` suppresses the status
// line, for saves the user did not ask for rather than ones they did.
bool saveCurrent(UiRuntime& ui, bool quiet = false);

}
