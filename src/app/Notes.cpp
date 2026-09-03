#include "app/Notes.h"

#include "app/Shell.h"
#include "app/WikiLinks.h"

#include <string>
#include <vector>

namespace micronotes::app {

void selectNoteAt(UiRuntime& ui, int index) {
  auto notes = ui.state.currentNotes();
  if(notes.empty()) return;
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  index = std::clamp(index, 0, static_cast<int>(notes.size()) - 1);
  ui.noteCursor = index;
  ui.state.selectNote(notes[static_cast<std::size_t>(index)].id);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    const auto recovered = ui.state.selectedRecoveryBody();
    ui.editor.setText(recovered ? *recovered : note->body);
    if(recovered && *recovered != note->body) ui.editor.markDirty();
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = recovered && *recovered != note->body ? "Recovered unsaved " + note->metadata.title : "Loaded " + note->metadata.title;
    ui.state.noteOpened(note->metadata.id);
  }
}

void selectTag(UiRuntime& ui, const std::string& tag) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectTag(tag);
  selectNoteAt(ui, 0);
}

void selectNoteById(UiRuntime& ui, const std::string& noteId) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectNote(noteId);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    const auto recovered = ui.state.selectedRecoveryBody();
    ui.editor.setText(recovered ? *recovered : note->body);
    if(recovered && *recovered != note->body) ui.editor.markDirty();
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = recovered && *recovered != note->body ? "Recovered unsaved " + note->metadata.title : "Loaded " + note->metadata.title;
    ui.state.noteOpened(note->metadata.id);
  }
}

void loadSelectedIntoEditor(UiRuntime& ui) {
  ui.clearBlockSelection();
  if(auto note = ui.state.selectedNote()) {
    if(ui.loadedNoteId != note->metadata.id || !ui.editor.dirty()) {
      ui.loadedNoteId = note->metadata.id;
      ui.editor.setText(note->body);
    }
  }
}

void createNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Start with --library <path> before creating notes";
    return;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  const auto folder = ui.state.selection().folder;
  invalidateWikiNotes(ui);
  // An empty body, not a `# Untitled` heading. The page draws the note's name
  // above its first block, so seeding one only put the name on screen twice and
  // left the caret on the second copy; the empty page prompts for a first line
  // instead, which is where the caret already is.
  if(auto created = ui.state.createNote("Untitled", folder, "")) {
    ui.loadedNoteId = created->id;
    ui.editor.setText("");
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = true;
    ui.focus = FocusArea::Editor;
    ui.status = "Created " + created->title;
  }
}

void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  const auto previousFolder = ui.state.selection().folder;
  ui.state.selectFolder(folder);
  createNote(ui);
  if(ui.state.selection().noteId.empty()) ui.state.selectFolder(previousFolder);
}

bool saveCurrent(UiRuntime& ui, bool quiet) {
  if(!ui.state.hasLibrary()) {
    if(!quiet) ui.status = "No library open";
    return false;
  }
  if(ui.state.selection().noteId.empty()) {
    createNote(ui);
  }
  if(ui.state.saveSelectedNote(ui.editor.text())) {
    ui.editor.markSaved();
    // Named by the library, not by the first line of the buffer. The title
    // lives in the note's header, and a note whose body happens to be empty is
    // still not called "Untitled".
    if(!quiet) {
      const auto note = ui.state.findNote(ui.state.selection().noteId);
      ui.status = "Saved " + (note ? note->title : std::string("note"));
    }
    return true;
  }
  ui.status = quiet ? "Autosave failed" : "Save failed";
  return false;
}

}
