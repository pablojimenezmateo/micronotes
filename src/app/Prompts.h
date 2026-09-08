#pragma once

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
void beginTagEdit(UiRuntime& ui);
void saveTags(UiRuntime& ui);
void beginFolderCreate(UiRuntime& ui);
void beginFolderRename(UiRuntime& ui);
void saveFolderRename(UiRuntime& ui);
void openLibraryPrompt(UiRuntime& ui);

// Confirms, and what they run when confirmed.
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
void openFolderPalette(UiRuntime& ui);
void openTrashPalette(UiRuntime& ui);
void openShortcutHelp(UiRuntime& ui);

}
