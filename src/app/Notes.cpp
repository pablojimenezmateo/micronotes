#include "app/Notes.h"

#include "app/Shell.h"
#include "app/WikiLinks.h"

#include "ui/TextUtil.h"

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
  ui.wikiNotesValid = false;
  if(auto created = ui.state.createNote("Untitled", folder, "# Untitled\n\n")) {
    ui.loadedNoteId = created->id;
    ui.editor.setText("# Untitled\n\n");
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
    if(!quiet) ui.status = "Saved " + ui::trimTitle(ui.editor.text());
    return true;
  }
  ui.status = quiet ? "Autosave failed" : "Save failed";
  return false;
}

}
