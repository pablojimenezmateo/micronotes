#pragma once

#include <filesystem>
#include <string>

// The overlays that ask a question: the palettes, the text prompts, and the two
// destructive confirms.
//
// Every one of them is the same three steps -- fill an `ui::Overlay`, push it on
// the stack, and refuse with a status line when there is nothing to ask about --
// and thirteen of them were `static` in Application.cpp, interleaved with the
// key handler and the command chain that open them. Together they are the
// shell's whole vocabulary for asking, which is worth being able to read in one
// place: the placeholders, the hints and the widths are a house style, and a
// new prompt that ignores it is obvious here and invisible spread across two
// thousand lines.
//
// The three `save*` verbs are here with the prompts they answer. A prompt and
// the commit that closes it are one behaviour -- `beginTagEdit` writes the
// current tags into the field and `saveTags` reads them back out -- and split
// apart they were free to disagree about what the field held.
namespace micronotes::app {

struct UiRuntime;

// Text prompts, and the commits that close them.

// The name a new note gets, asked for before the note exists.
//
// Creating one used to make `Untitled` straight away and leave the reader to
// rename it: two gestures for one intention, and a library full of `Untitled`
// files every time the second was forgotten.
void beginNoteCreate(UiRuntime& ui);
// The same prompt, reopened with what was typed and something to say about it.
void beginNoteCreate(UiRuntime& ui, std::string value, std::string hint);
void saveNoteCreate(UiRuntime& ui, const std::string& asked);

void beginTagEdit(UiRuntime& ui);
void saveTags(UiRuntime& ui);
void beginFolderCreate(UiRuntime& ui);
void beginFolderRename(UiRuntime& ui);
void saveFolderRename(UiRuntime& ui);
void openLibraryPrompt(UiRuntime& ui);

// Companion files -- see `library::kFilesDirName`. All of these are about
// `ui.sidebar.companionTarget`, the row whose menu opened them. The commits
// take the typed text directly rather than through a field: a companion has no
// inline field of its own, and the prompt is the only way in.
void beginCompanionRename(UiRuntime& ui);
void saveCompanionRename(UiRuntime& ui, const std::string& name);
void beginCompanionFolderCreate(UiRuntime& ui);
void saveCompanionFolderCreate(UiRuntime& ui, const std::string& name);
// "Move to notebook" for a file: the notebook chosen is where its `files/` is.
void openCompanionMovePalette(UiRuntime& ui);
void moveCompanionToNotebook(UiRuntime& ui, const std::filesystem::path& notebook);
void openDeleteCompanionConfirm(UiRuntime& ui);
void deleteCompanionTarget(UiRuntime& ui);

// Confirms, and what they run when confirmed.

// Deleting a tag, which is not deleting a thing: a tag exists because notes
// carry it, so removing one rewrites the front matter of every note that does.
// The open note goes through the guarded save with the buffer's body, so an
// unsaved edit is not thrown away by a change to a list of labels.
void openDeleteTagConfirm(UiRuntime& ui, std::string tag);
void deleteTag(UiRuntime& ui, const std::string& tag);

void openDeleteNoteConfirm(UiRuntime& ui);
void openDeleteFolderConfirm(UiRuntime& ui);
void deleteSelected(UiRuntime& ui);
void deleteSelectedFolder(UiRuntime& ui);

// Filterable lists.
void openCommandPalette(UiRuntime& ui);
// One list of every note in the library, reused by "go to note" and by anything
// that has to name a target note. `id` says which, so the result knows what it
// is answering.
void openNotePalette(UiRuntime& ui, std::string overlayId, std::string title);
// One list of every notebook. `overlayId` says what the choice is for -- moving
// a note, or moving a file -- and `title` says so to the reader.
void openFolderPalette(UiRuntime& ui, std::string overlayId = "move-note-folder",
                       std::string title = "Move note to");
void openTrashPalette(UiRuntime& ui);

}
